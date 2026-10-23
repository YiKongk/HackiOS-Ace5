/* SM8650 clock/GMU startup, following OnePlus gpucc-pineapple.c and gen7_gmu.
 * Finite diagnostics only; GX power is tested, but no rendering is submitted. */
struct a750_fastclock {
    uint32_t pll[13], vote, gmu_cfg, hub_cfg;
    bool owned;
};
static const uint32_t a750_pll_offsets[]={0x1000,0x1004,0x1010,0x1014,0x1018,0x101c,
    0x1020,0x1024,0x1028,0x102c,0x1030,0x1034,0x1038};
static bool a750_root(vm_offset_t cc, uint32_t cmd, uint32_t cfg)
{
    a750_wr(cc,cmd+4,cfg);
    a750_bits(cc,cmd,1,1);
    return a750_poll(cc,cmd,1,0);
}
static bool a750_fastclock_off(struct a750_fastclock *s, vm_offset_t gcc, vm_offset_t cc)
{
    if (!s->owned) return true;
    bool good=a750_root(cc,0x9318,s->gmu_cfg);
    good=a750_root(cc,0x93ec,s->hub_cfg) && good;
    if (!good) {
        a750_log("ace5-gpu: root restore failed; retain parent clocks until power off\n"); return false;
    }
    a750_bits(cc,0x1000,1,0); a750_wr(cc,0x1004,0); a750_bits(cc,0x1000,4,0);
    for (unsigned i=2;i<13;i++) a750_wr(cc,a750_pll_offsets[i],s->pll[i]);
    a750_wr(cc,0x1004,s->pll[1]); a750_bits(cc,0x1000,5,s->pll[0]);
    a750_bits(gcc,0x52000,1<<15,s->vote);
    s->owned=false;
    a750_log("ace5-gpu: operating clocks returned to XO; PLL1 stopped\n");
    return true;
}
static bool a750_fastclock_on(struct a750_fastclock *s, vm_offset_t gcc, vm_offset_t cc)
{
    for (unsigned i=0;i<13;i++) s->pll[i]=a750_rd(cc,a750_pll_offsets[i]);
    s->vote=a750_rd(gcc,0x52000); s->gmu_cfg=a750_rd(cc,0x931c); s->hub_cfg=a750_rd(cc,0x93f0);
    if ((s->pll[0]&5) || s->pll[1] || (s->pll[4]&(1<<25)) || s->gmu_cfg || s->hub_cfg) {
        a750_log("ace5-gpu: PLL1/root already owned; operating clock test refused\n"); return false;
    }
    s->owned=true;
    /* Vendor's 520 MHz Lucid OLE configuration, divided by 2 by the GMU RCG.
     * Keep the bootloader's shared CX/MX rail votes; no voltage is lowered. */
    const uint32_t config[]={0x4444001b,0x1555,7,5,0x20485699,0x00182261,
        0x82aa299c,0,3,0x9000,0x34};
    for (unsigned i=2;i<13;i++) a750_wr(cc,a750_pll_offsets[i],config[i-2]);
    a750_bits(cc,0x1000,4,4); a750_wr(cc,0x1004,1);
    if (!a750_poll(cc,0x1000,0x80000000,0x80000000)) return false;
    a750_bits(cc,0x1000,1,1);
    a750_bits(gcc,0x52000,1<<15,1<<15);
    if (!a750_root(cc,0x93ec,0x505) || !a750_root(cc,0x9318,0x303)) return false;
    a750_read(cc,0x1000,"PLL1_LOCKED");
    a750_read(cc,0x931c,"GMU_260MHZ_CFG"); a750_read(cc,0x93f0,"HUB_200MHZ_CFG");
    a750_log("ace5-gpu: stage 9 complete; PLL locked, GMU/HUB configured for 260/200 MHz\n");
    return true;
}

static int32_t a750_scm3(uint64_t function, uint64_t arginfo,
    uint64_t p0, uint64_t p1, uint64_t p2, uint64_t *answer)
{
    uint64_t session=0, next=function, began=mach_absolute_time();
    *answer=0;
    for (unsigned i=0,busy=0;i<10000;i++) {
        register uint64_t x0 __asm__("x0")=next;
        register uint64_t x1 __asm__("x1")=arginfo;
        register uint64_t x2 __asm__("x2")=p0;
        register uint64_t x3 __asm__("x3")=p1;
        register uint64_t x4 __asm__("x4")=p2;
        register uint64_t x5 __asm__("x5")=0;
        register uint64_t x6 __asm__("x6")=session;
        register uint64_t x7 __asm__("x7")=0;
        __asm__ volatile("smc #0":"+r"(x0),"+r"(x1),"+r"(x2),"+r"(x3),"+r"(x4),"+r"(x5),"+r"(x6),"+r"(x7)
            ::"x8","x9","x10","x11","x12","x13","x14","x15","x16","x17","memory");
        if (x0==1) { next=1; session=x6; }
        else if ((int32_t)x0==-12 && busy++<20) {
            next=function; session=0; delay_for_interval(30,NSEC_PER_MSEC);
        } else { *answer=x1; return (int32_t)x0; }
        uint64_t elapsed=0;
        absolutetime_to_nanoseconds(mach_absolute_time()-began,&elapsed);
        if (elapsed>5ull*NSEC_PER_SEC) break;
    }
    return -1000; /* interrupted call did not reach a known completion */
}
static int32_t a750_scm(uint64_t function, uint64_t value, uint64_t *answer)
{
    return a750_scm3(function,1,value,0,0,answer);
}
#include "giulia_gmu_power.h"
#include "giulia_gmu_hfi.h"
#include "giulia_gpu_memory.h"
#include "giulia_zap_start.h"
#include "giulia_gpu_start.h"
static bool a750_gmu_start(struct a750_memory *m, vm_offset_t gmu, vm_offset_t smmu, unsigned stage)
{
    vm_offset_t gbif=ml_io_map(0x3d00000,0x10000);
    if (!gbif) return false;
    uint32_t halt=a750_read(gbif,0xf114,"GBIF_HALT_BEFORE_BOOT");
    uint64_t answer=0;
    a750_log("ace5-gpu: querying vendor SCM GPU init-regs service\n");
    int32_t result=a750_scm(0xc2000601,0x02002801,&answer);
    a750_log("ace5-gpu: SCM availability status=%d answer=%#llx\n",result,answer);
    if (result || !answer) return false;
    /* Vendor GPU_ALWAYS_EN_REQ | GPU_TSENSE_EN_REQ. BCL/CLX are not requested. */
    result=a750_scm(0x42002801,9,&answer);
    a750_log("ace5-gpu: SCM GPU init status=%d answer=%#llx\n",result,answer);
    if (result) return false;

    uint32_t *hfi=(uint32_t *)a750_find_memory(m,0x60020000,0x12000);
    if (!hfi) return false;
    hfi[0]=0; hfi[1]=210; hfi[2]=6; hfi[3]=12; hfi[4]=17; hfi[5]=17;
    for (unsigned i=0;i<17;i++) {
        uint32_t *q=hfi+6+12*i;
        unsigned priority=i<7?0:i<11?1:i<14?2:i<16?3:4;
        q[0]=1; q[1]=0x60020000+848+i*4096; q[2]=i|(priority<<8); q[3]=1024;
    }
    a750_flush((vm_offset_t)hfi,0x12000);
    /* Gen7_9_x CX counter is independent of the GMU reset. Seed at 19.2 MHz. */
    uint64_t freq=__builtin_arm_rsr64("CNTFRQ_EL0");
    if (freq!=19200000) { a750_log("ace5-gpu: unexpected timer frequency=%llu\n",freq); return false; }
    uint64_t tick=__builtin_arm_rsr64("CNTVCT_EL0");
    a750_wr(gmu,0x36200,(uint32_t)tick); a750_wr(gmu,0x36204,(uint32_t)(tick>>32));
    uint32_t t0=a750_read(gmu,0x36200,"CX_AO_COUNTER");
    delay_for_interval(100,NSEC_PER_USEC);
    a750_log("ace5-gpu: CX timer seed-register delta=%u (not a live counter)\n",a750_rd(gmu,0x36200)-t0);

    /* Documented CM3/AHB config, only CX registers. GPU/GX stays collapsed. */
    const uint32_t offsets[]={0x163c4,0x163c0,0x16070,0x16004,0x16614,0x16610,
        0x26c44,0x160b4,0x16738,0x1673c,0x16734,0x1503c,0x15000,0x15004,0x1664c,0x26c18,0x16618};
    const uint32_t values[]={1,1,0,2,0x60020000,1,0x80c808a0,0x4052,0,0x07090100,
        0x6000200f,1,1,1,0xffffffff,0xffffffff,0};
    uint32_t saved[sizeof(offsets)/sizeof(offsets[0])];
    for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        saved[i]=a750_rd(gmu,offsets[i]);
        a750_wr(gmu,offsets[i],values[i]);
    }
    struct a750_power power={0};
    if (stage>=13 && !a750_power_prepare(&power,gmu)) return false;
    /* Vendor register_config explicitly clears retained GBIF halts before boot. */
    a750_wr(gbif,0xf114,0);
    a750_read(gbif,0xf114,"GBIF_HALT_CLEARED");
    a750_log("ace5-gpu: releasing CM3 reset with owned firmware mappings\n");
    a750_wr(gmu,0x16000,0);
    bool ready=false;
    for (unsigned i=0;i<5000;i++) {
        if ((a750_rd(gmu,0x16070)&0x1ff)==0x100) { ready=true; break; }
        if (a750_rd(smmu,0x22058)&0xc00001fe) break;
        delay_for_interval(100,NSEC_PER_USEC);
    }
    struct a750_hfi session={0};
    bool hfi_good=true;
    if (ready && stage>=11) hfi_good=a750_hfi_query(&session,m,gmu,smmu,stage);
    a750_read(gmu,0x16070,"CM3_FW_INIT_RESULT"); a750_read(gmu,0x16068,"CM3_FW_BUSY");
    a750_read(gmu,0x26c14,"GMU_AO_INTERRUPT_STATUS");
    uint32_t faults=a750_read(smmu,0x22058,"GMU_CB_FSR_AFTER_BOOT");
    ready=ready && !(faults&0xc00001fe);
    a750_read(smmu,0x22060,"GMU_CB_FAR_LO"); a750_read(smmu,0x22064,"GMU_CB_FAR_HI");
    a750_read(smmu,0x22068,"GMU_CB_FSYNR0");
    a750_wr(gmu,0x16000,1);
    a750_runtime_active=false; /* SQE halted and CM3 reset; panic hook no longer needs GX */
    bool reset=a750_poll(gmu,0x16000,1,1);
    if (stage>=13 && !a750_power_sleep(&power,gmu))
        panic("ace5-gpu: RSC sleep failed; retain firmware mappings");
    if (stage>=15 && !(a750_read(gmu,0x16340,"GFX_PWR_CLK_AFTER_SLEEP")&1))
        panic("ace5-gpu: GX remained powered after RSC sleep; retain mappings");
    a750_wr(gbif,0xf114,2); /* vendor CX arbiter halt: drain outstanding DDR traffic */
    bool drained=a750_poll(gbif,0xf118,2,2);
    a750_log("ace5-gpu: GMU DDR traffic drained=%u\n",drained);
    if (!reset || !drained) panic("ace5-gpu: GMU did not quiesce; retain mappings and return to loader");
    a750_gx_release(&session);
    a750_log("ace5-gpu: stage 10 %s; firmware ready=%u; reset reasserted=%u\n",ready?"complete":"FAILED",ready,reset);
    for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) a750_wr(gmu,offsets[i],saved[i]);
    a750_wr(gbif,0xf114,halt);
    a750_power_restore(&power);
    return ready && reset && hfi_good;
}
