/* SM8650 / giulia Adreno 750v2 bring-up and optional rendering backend.
 * Register facts: PKG110 live DT and OnePlusOSS pineapple GCC/GPUCC drivers.
 * ace5_gpu_probe=1 reads external clocks; =2 reads GPUCC; =3 cycles CX power;
 * =4 enables CX clocks at XO speed and reads GMU/SMMU IDs; =5 tests and restores
 * two words in each GMU TCM; =6 reads stream/PLL state; =7 stages local firmware;
 * =8 configures and tears down SID5/CB2; =9 uses 260/200 MHz clocks; =10 boots
 * CM3 then resets it; =11 queries HFI; =12 supplies power tables; =13 starts
 * core firmware; =14 handles scheduler allocations; =15 powers GX at 231 MHz;
 * =16 prepares GPU CB0 and firmware backing; =17 tests CP/DMA; with direct
 * mode =18 authenticates ZAP, =19 tests nonsecure CP DMA, =20 submits a BR/BV IB.
 * ace5_gpu_keep=1 retains direct stage 20 and publishes the RedwoodGPU service.
 * gpu=1 must never fall through to the redwood A642L path on this board.
 */
#include <kern/clock.h>
#include <kern/locks.h>
#include <kern/sched_prim.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <sys/systm.h>
#include <sys/redwood_gpu_backend.h>
#include <vm/memory_types.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern vm_offset_t pe_arm_get_soc_base_phys(void);
extern void loader_log_line(const char *);
extern kern_return_t thread_terminate(thread_t);
/* printf's kernel message buffer is appended only at loader return. Mirror
 * each driver line immediately so a host snapshot can precede a fatal MMIO. */
static int a750_log(const char *format, ...) __printflike(1,2);
static int a750_log(const char *format, ...)
{
    char line[384];
    va_list args;
    va_start(args,format);
    int length=vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    loader_log_line(line);
    printf("%s",line);
    return length;
}
static uint32_t probe_pause;
static uint32_t a750_gpu_direct;
static uint32_t a750_gpu_keep;
static bool a750_runtime_active;
static uint32_t a750_rd(vm_offset_t base, uint32_t off) { return *(volatile uint32_t *)(base+off); }
/* Keep the store source in an ordinary argument register: some Qualcomm
 * hypervisor SMMU emulation versions mishandle stores from WZR (Linux quirk). */
static __attribute__((noinline)) void a750_wr(vm_offset_t base, uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(base+off)=v;
    __asm__ volatile("dsb sy" ::: "memory");
}
static void a750_bits(vm_offset_t base, uint32_t off, uint32_t mask, uint32_t value) {
    a750_wr(base,off,(a750_rd(base,off)&~mask)|(value&mask));
}
static bool a750_poll(vm_offset_t base, uint32_t off, uint32_t mask, uint32_t value) {
    for (unsigned n=0;n<2000;n++) {
        if ((a750_rd(base,off)&mask)==value) return true;
        delay_for_interval(10,NSEC_PER_USEC);
    }
    a750_log("ace5-gpu: timeout offset=%#x value=%#x expected=%#x mask=%#x\n",off,a750_rd(base,off),value,mask);
    return false;
}

static uint32_t a750_read(vm_offset_t base, uint32_t offset, const char *name)
{
    a750_log("ace5-gpu: read %s +%#x\n", name, offset);
    if (probe_pause) delay_for_interval(probe_pause, NSEC_PER_MSEC);
    uint32_t value=*(volatile uint32_t *)(base+offset);
    a750_log("ace5-gpu: %s=%#x\n", name, value);
    return value;
}

#include "giulia_gmu_memory.h"
#include "giulia_gmu_start.h"
#include "giulia_gpu_live.h"
#include "giulia_gpu_owner.h"

static bool a750_smmu_probe(struct a750_memory *m, vm_offset_t smmu, vm_offset_t gmu, unsigned stage)
{
    /* OnePlus arm-smmu-qcom.c reserves CB0/1 for GPU; other clients start at 2.
     * Only take the observed idle CB2/SMR5. Never scan secure context banks. */
    const unsigned context=2, stream=5;
    const uint32_t cb=0x22000, cbar=0x1008, cba2r=0x1808;
    if (a750_rd(smmu,0x20)!=0x4c017e18 || a750_rd(smmu,0x24)!=0x40000016 ||
        a750_rd(smmu,0x28)!=0x5511 || (a750_rd(smmu,0)&1)) return false;
    for (unsigned i=0;i<24;i++) if (a750_rd(smmu,0x800+4*i)&0x80000000) {
        a750_log("ace5-gpu: SMMU has an existing stream; refusing ownership\n"); return false;
    }
    a750_log("ace5-gpu: inspect idle GMU context bank 2\n");
    uint32_t control=a750_read(smmu,cb,"GMU_CB_SCTLR");
    if (control&1) return false;
    if (a750_read(smmu,cb+0x58,"GMU_CB_FSR")&0xc00001fe) return false;
    const uint32_t offsets[]={cba2r,cbar,cb+0x10,cb+0x30,cb+0x20,cb+0x24,
        cb+0x28,cb+0x2c,cb+0x38,cb+0x3c,cb+4};
    uint64_t root=kvtophys(m->table[0])|((uint64_t)context<<48);
    const uint32_t values[]={1,0x1f300,0x38011,0x803519,(uint32_t)root,(uint32_t)(root>>32),
        0,0,0xf404ff44,0,0x32b}; /* live DT qcom,actlr for SID 0..0x3ff */
    const uint32_t masks[]={1,0x3f3ff,0x3801f,0x803fff,0xfffff000,0xffff000f,
        0xfffff000,0xffff000f,0xffffffff,0xffffffff,0xffffffff};
    uint32_t smr=a750_rd(smmu,0x800+4*stream), s2cr=a750_rd(smmu,0xc00+4*stream);
    a750_wr(smmu,cb,0);
    bool good=true;
    for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        a750_wr(smmu,offsets[i],values[i]);
        uint32_t back=a750_rd(smmu,offsets[i]);
        a750_log("ace5-gpu: SMMU +%#x wrote=%#x read=%#x\n",offsets[i],values[i],back);
        if ((back&masks[i])!=(values[i]&masks[i])) { good=false; break; }
    }
    if (good) {
        a750_wr(smmu,cb+0x610,context);
        a750_wr(smmu,cb+0x7f0,0xffffffff);
        good=a750_poll(smmu,cb+0x7f4,1,0);
    }
    if (good) {
        /* This AArch64 bank reads ASIDPNE (bit 12) as zero. Validate the
         * implemented enable/fault bits, excluding that RAZ bit and RES0 bit 3. */
        a750_wr(smmu,cb,0x27); /* no stall, faults recorded, no interrupt */
        a750_wr(smmu,0xc00+4*stream,context);
        a750_wr(smmu,0x800+4*stream,0x80000005); /* exact SID5, DT mask=0 */
        uint32_t enabled=a750_read(smmu,cb,"GMU_CB_ENABLED");
        uint32_t match=a750_read(smmu,0x800+4*stream,"GMU_STREAM_MATCH");
        uint32_t route=a750_read(smmu,0xc00+4*stream,"GMU_STREAM_ROUTE");
        good=(enabled&0x1f7)==0x27 && match==0x80000005 && (route&0x300ff)==context;
        a750_log("ace5-gpu: GMU stream 5 -> CB2, root=%#llx, register verification=%u\n",root,good);
        /* IDR0.ATOSNS=1: ATS1PR/PAR translation probes are not supported. */
        a750_log("ace5-gpu: hardware ATS unavailable; CM3 still in reset, DMA not yet verified\n");
        if (good && stage>=10) good=a750_gmu_start(m,gmu,smmu,stage);
    }
    a750_wr(smmu,0xc00+4*stream,0x20000);
    a750_wr(smmu,0x800+4*stream,smr);
    a750_wr(smmu,cb,0);
    a750_wr(smmu,cb+0x58,a750_rd(smmu,cb+0x58)&0xc00001fe);
    a750_wr(smmu,cb+0x610,context);
    a750_wr(smmu,cb+0x7f0,0xffffffff);
    bool synced=a750_poll(smmu,cb+0x7f4,1,0);
    /* CBAR reset contents may describe stage 2, which the hypervisor rejects.
     * Like Linux domain teardown, leave the owned bank disabled instead of
     * replaying uninitialized context configuration. Remove our RAM pointers. */
    a750_wr(smmu,cb+0x20,0); a750_wr(smmu,cb+0x24,0);
    a750_wr(smmu,cb+0x28,0); a750_wr(smmu,cb+0x2c,0);
    a750_wr(smmu,0xc00+4*stream,s2cr);
    bool restored=synced && !(a750_rd(smmu,cb)&1) && a750_rd(smmu,cb+0x20)==0 &&
        a750_rd(smmu,cb+0x24)==0 && a750_rd(smmu,0x800+4*stream)==smr &&
        a750_rd(smmu,0xc00+4*stream)==s2cr;
    a750_log("ace5-gpu: SMMU stream restored, context disabled and RAM pointers cleared=%u\n",restored);
    return good && restored;
}

static bool a750_probe(void)
{
    DTEntry entry; const void *prop; unsigned size;
    if (SecureDTLookupEntry(NULL,"/arm-io/gic",&entry)!=kSuccess ||
        SecureDTGetProperty(entry,"reg",&prop,&size)!=kSuccess ||
        size<4*sizeof(uint64_t)) return false;
    const uint64_t *reg=prop;
    if (pe_arm_get_soc_base_phys()+reg[0]!=0x17100000 ||
        pe_arm_get_soc_base_phys()+reg[2]!=0x17180000) return false;

    uint32_t stage=0, legacy=0, gpu=0;
    PE_parse_boot_argn("ace5_gpu_probe",&stage,sizeof(stage));
    PE_parse_boot_argn("ace5_gpu_direct",&a750_gpu_direct,sizeof(a750_gpu_direct));
    PE_parse_boot_argn("adreno",&legacy,sizeof(legacy));
    PE_parse_boot_argn("gpu",&gpu,sizeof(gpu));
    if (legacy || gpu) a750_log("ace5-gpu: A642L driver blocked on giulia; acceleration is not implemented\n");
    if (!stage) return true;
    if (stage>20 || (stage>=18 && !a750_gpu_direct)) {
        a750_log("ace5-gpu: unsupported stage/mode %u; ZAP requires direct CP mode\n",stage); return true;
    }
    if (a750_gpu_keep && (stage!=20 || !a750_gpu_direct)) return true;
    uint32_t bootcpu=0, seconds=0;
    PE_parse_boot_argn("bootcpu",&bootcpu,sizeof(bootcpu));
    PE_parse_boot_argn("loader_return_after",&seconds,sizeof(seconds));
    if (stage>=3 && (bootcpu!=1 || !seconds)) {
        a750_log("ace5-gpu: power/memory probes require CPU 1 and a finite return timer\n");
        return true;
    }
    uint32_t step_delay=0;
    PE_parse_boot_argn("ace5_gpu_step_delay",&step_delay,sizeof(step_delay));
    if (a750_gpu_keep && step_delay) {
        a750_log("ace5-gpu: persistent mode requires zero register trace delay\n");return true;
    }
    if (stage>=17 && (step_delay>3000 || seconds<10+(240*step_delay+999)/1000)) {
        a750_log("ace5-gpu: trace delay exceeds the return timer; no hardware touched\n");
        return true;
    }
    PE_parse_boot_argn("ace5_gpu_pause",&probe_pause,sizeof(probe_pause));
    if (probe_pause>3000) probe_pause=3000;
    a750_log("ace5-gpu: Adreno 750v2 probe stage=%u\n",stage);
    vm_offset_t gcc=ml_io_map(0x100000,0xc0000);
    if (!gcc) { a750_log("ace5-gpu: GCC mapping failed\n"); return true; }
    a750_read(gcc,0x71004,"GCC_GPU_CFG_AHB_CBCR");
    a750_read(gcc,0x71010,"GCC_GPU_MEMNOC_GFX_CBCR");
    a750_read(gcc,0x71018,"GCC_GPU_SNOC_DVM_GFX_CBCR");
    a750_read(gcc,0x52000,"GCC_PLL_ENA_VOTE");
    a750_log("ace5-gpu: stage 1 complete\n");
    if (stage<2) return true;
    vm_offset_t cc=ml_io_map(0x3d90000,0xa000);
    if (!cc) { a750_log("ace5-gpu: GPUCC mapping failed\n"); return true; }
    a750_read(cc,0x9108,"GPU_CC_CX_GDSCR");
    a750_read(cc,0x9168,"GPU_CC_CX_GDSC_HW_CTRL");
    a750_read(cc,0x905c,"GPU_CC_GX_GDSCR");
    a750_read(cc,0x911c,"GPU_CC_AHB_CBCR");
    a750_read(cc,0x913c,"GPU_CC_CX_GMU_CBCR");
    a750_read(cc,0x9144,"GPU_CC_CXO_CBCR");
    a750_read(cc,0x93e8,"GPU_CC_HUB_AON_CBCR");
    a750_read(cc,0x9148,"GPU_CC_HUB_CX_INT_CBCR");
    a750_read(cc,0x9318,"GPU_CC_GMU_CMD_RCGR");
    a750_read(cc,0x931c,"GPU_CC_GMU_CFG_RCGR");
    a750_read(cc,0x93ec,"GPU_CC_HUB_CMD_RCGR");
    a750_read(cc,0x93f0,"GPU_CC_HUB_CFG_RCGR");
    a750_read(cc,0x942c,"GPU_CC_HUB_DIV");
    a750_log("ace5-gpu: stage 2 complete; no GPU registers written\n");
    if (stage<3) return true;

    /* CX is separate from GX. GMU stage 15 uses stock RPMh operating points.
     * Only collapse/clock-enable bits we own are restored, including on timeout. */
    uint32_t cx=a750_rd(cc,0x9108);
    if (!(cx&1) || (cx&2)) {
        a750_log("ace5-gpu: CX already active or hardware-controlled; refusing to take ownership\n");
        return true;
    }
    a750_log("ace5-gpu: enabling CX power; GX remains collapsed\n");
    a750_bits(cc,0x9108,1,0);
    delay_for_interval(2,NSEC_PER_USEC);
    if (!a750_poll(cc,0x9168,0x80000000,0x80000000)) goto power_off;
    a750_read(cc,0x9108,"GPU_CC_CX_GDSCR_ON");
    a750_read(cc,0x9168,"GPU_CC_CX_HW_CTRL_ON");
    a750_log("ace5-gpu: stage 3 complete; CX power acknowledged\n");
    if (stage<4) goto power_off;

    /* Retain the boot firmware's XO roots. These are diagnostic clocks only,
     * not the 260/625 MHz operating points required to start GMU firmware. */
    if (a750_rd(cc,0x931c)!=0 || a750_rd(cc,0x93f0)!=0) {
        a750_log("ace5-gpu: unexpected root configuration; clock experiment skipped\n");
        goto power_off;
    }
    struct { uint32_t off,mask,saved; } clocks[]={
        {0x9318,2,0},{0x93ec,2,0},{0x93e8,1,0},{0x911c,1,0},
        {0x9148,1,0},{0x913c,1,0},{0x9144,1,0},{0x7000,1,0}
    };
    unsigned changed=0;
    for (unsigned i=0;i<sizeof(clocks)/sizeof(clocks[0]);i++) {
        clocks[i].saved=a750_rd(cc,clocks[i].off);
        a750_log("ace5-gpu: clock enable offset=%#x saved=%#x\n",clocks[i].off,clocks[i].saved);
        a750_bits(cc,clocks[i].off,clocks[i].mask,clocks[i].mask); changed++;
        delay_for_interval(10,NSEC_PER_USEC);
        if (!a750_poll(cc,clocks[i].off,0x80000000,0)) goto clocks_off;
    }
    vm_offset_t gmu=ml_io_map(0x3d68000,0x37000);
    vm_offset_t smmu=ml_io_map(0x3da0000,0x40000);
    if (!gmu || !smmu) { a750_log("ace5-gpu: GMU/SMMU mapping failed\n"); goto clocks_off; }
    a750_read(gmu,0x16000,"GMU_CM3_SYSRESET");
    a750_read(gmu,0x16070,"GMU_CM3_FW_INIT_RESULT");
    uint32_t before=a750_read(gmu,0x16220,"GMU_ALWAYS_ON_COUNTER_L");
    delay_for_interval(1000,NSEC_PER_USEC);
    uint32_t after=a750_read(gmu,0x16220,"GMU_ALWAYS_ON_COUNTER_L");
    a750_read(smmu,0x20,"GPU_SMMU_IDR0");
    a750_read(smmu,0x24,"GPU_SMMU_IDR1");
    a750_log("ace5-gpu: stage 4 complete; counter delta=%u; no firmware or commands submitted\n",after-before);
    if (stage>=5) {
        if (!(a750_rd(gmu,0x16000)&1)) {
            a750_log("ace5-gpu: GMU is not in reset; memory test refused\n");
            goto clocks_off;
        }
        /* GEN7_GMU_CM3_{I,D}TCM_START = word 0x1b400/0x1c400 from GPU base.
         * Offset below is relative to the DT GMU aperture at GPU + 0x68000. */
        const uint32_t offsets[]={0x5000,0x9000}, pattern[]={0x5aa5f00d,0xc33c0ff0};
        bool good=true;
        for (unsigned t=0;t<2;t++) {
            uint32_t saved[2], readback[2];
            for (unsigned i=0;i<2;i++) saved[i]=a750_rd(gmu,offsets[t]+4*i);
            a750_log("ace5-gpu: %sTCM write/read/restore at offset=%#x\n",t?"D":"I",offsets[t]);
            for (unsigned i=0;i<2;i++) a750_wr(gmu,offsets[t]+4*i,pattern[i]);
            for (unsigned i=0;i<2;i++) readback[i]=a750_rd(gmu,offsets[t]+4*i);
            for (unsigned i=0;i<2;i++) a750_wr(gmu,offsets[t]+4*i,saved[i]);
            for (unsigned i=0;i<2;i++) {
                if (readback[i]!=pattern[i] || a750_rd(gmu,offsets[t]+4*i)!=saved[i]) good=false;
            }
            a750_log("ace5-gpu: %sTCM readback=%#x,%#x; restored=%#x,%#x\n",
                t?"D":"I",readback[0],readback[1],a750_rd(gmu,offsets[t]),a750_rd(gmu,offsets[t]+4));
        }
        a750_log("ace5-gpu: stage 5 %s; GMU remains in reset\n",good?"complete":"FAILED");
        if (!good) goto clocks_off;
    }
    if (stage>=6) {
        a750_read(smmu,0,"GPU_SMMU_SCR0");
        uint32_t id0=a750_read(smmu,0x20,"GPU_SMMU_IDR0");
        a750_read(smmu,0x24,"GPU_SMMU_IDR1");
        a750_read(smmu,0x28,"GPU_SMMU_IDR2");
        a750_read(smmu,0x3c,"GPU_SMMU_IDR7");
        a750_read(smmu,0x48,"GPU_SMMU_GFSR");
        unsigned count=id0&0xff;
        if (count>128) goto clocks_off;
        for (unsigned i=0;i<count;i++) {
            a750_log("ace5-gpu: SMR[%u]=%#x S2CR=%#x\n",i,
                a750_rd(smmu,0x800+4*i),a750_rd(smmu,0xc00+4*i));
        }
        /* Only read the documented PLL configuration, not secure context banks. */
        const uint32_t pll[]={0x1000,0x1004,0x1008,0x100c,0x1010,0x1014,0x1018,0x101c,0x1020,0x1024};
        for (unsigned i=0;i<sizeof(pll)/sizeof(pll[0]);i++)
            a750_read(cc,pll[i],"GPU_CC_PLL1");
        a750_log("ace5-gpu: stage 6 complete; stream table read only\n");
    }
    if (stage>=7) {
        struct a750_memory memory={0};
        bool good=a750_memory_prepare(&memory) && a750_tcm_load(&memory,gmu);
        a750_log("ace5-gpu: stage 7 %s; firmware staged, processor held in reset\n",good?"complete":"FAILED");
        struct a750_fastclock fastclock={0};
        if (good && stage>=9) good=a750_fastclock_on(&fastclock,gcc,cc);
        if (good && stage>=8) {
            good=a750_smmu_probe(&memory,smmu,gmu,stage);
            a750_log("ace5-gpu: stage 8 %s; SMMU setup/readback/teardown\n",good?"complete":"FAILED");
        }
        if (!a750_fastclock_off(&fastclock,gcc,cc)) a750_log("ace5-gpu: clock cleanup FAILED\n");
        if (!a750_memory_release(&memory,gmu)) a750_log("ace5-gpu: memory cleanup FAILED\n");
    }
clocks_off:
    while (changed) {
        unsigned i=--changed;
        a750_bits(cc,clocks[i].off,clocks[i].mask,clocks[i].saved);
    }
power_off:
    a750_bits(cc,0x9108,1,cx);
    delay_for_interval(10,NSEC_PER_USEC);
    bool off=a750_poll(cc,0x9168,0x80000000,0);
    a750_read(cc,0x9108,"GPU_CC_CX_GDSCR_RESTORED");
    a750_read(cc,0x9168,"GPU_CC_CX_HW_CTRL_RESTORED");
    a750_log("ace5-gpu: probe ended; CX off=%u; software rendering continues\n",off);
    return true;
}
