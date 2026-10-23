/* Gen7_9_1 startup follows the device's pinned OnePlus KGSL sources.
 * All offsets in giulia_gpu_regs.h are DWORD register indices. */
#include "giulia_gpu_regs.h"
static uint32_t a750_gpu_step_delay;
static void a750_gpu_step(const char *operation, uint32_t reg, uint32_t value)
{
    if (!a750_gpu_step_delay) return;
    a750_log("ace5-gpu: CP trace %s register=%#x value=%#x\n",operation,reg,value);
    delay_for_interval(a750_gpu_step_delay,NSEC_PER_MSEC);
}
static void a750_gpu_write(vm_offset_t gx, vm_offset_t gmu, uint32_t reg, uint32_t value)
{
    a750_gpu_step("write",reg,value);
    if (reg>=0x1a000) a750_wr(gmu,reg*4-0x68000,value);
    else a750_wr(gx,reg*4,value);
}
#include "giulia_gpu_submit.h"
static bool a750_gpu_idle(struct a750_hfi *s, vm_offset_t gx)
{
    bool idle=false;
    for (unsigned i=0;i<2000;i++) {
        if (!(a750_rd(s->gmu,0x26c30)&(1<<23))) { idle=true; break; }
        if ((a750_rd(s->smmu,0x20058)&0xc00001fe) || (a750_rd(s->gmu,0x16648)&0xe00)) break;
        delay_for_interval(100,NSEC_PER_USEC);
    }
    uint32_t fsr=a750_read(s->smmu,0x20058,"GPU_CB0_FSR_CP");
    a750_read(s->smmu,0x20060,"GPU_CB0_FAR_LO");
    a750_read(s->smmu,0x20064,"GPU_CB0_FAR_HI");
    uint32_t fault=a750_read(gx,GEN7_CP_HW_FAULT*4,"CP_HW_FAULT");
    uint32_t interrupt=a750_read(gx,GEN7_CP_INTERRUPT_STATUS*4,"CP_INTERRUPT_STATUS");
    a750_read(gx,GEN7_CP_PROTECT_STATUS*4,"CP_PROTECT_STATUS");
    a750_read(gx,GEN7_RBBM_STATUS*4,"RBBM_STATUS_AFTER_CP");
    a750_read(gx,GEN7_RBBM_INT_0_STATUS*4,"RBBM_INT_0_STATUS");
    a750_read(s->gmu,0x26c30,"GPU_CX_BUSY_STATUS_AFTER_CP");
    return idle && !(fsr&0xc00001fe) && !fault && !interrupt;
}
#include "giulia_gpu_direct.h"
static bool a750_gx_start(struct a750_hfi *s)
{
    vm_offset_t gx=ml_io_map(0x3d00000,0x40000), gmu=s->gmu;
    if (!gx || !s->gx) return false;
    PE_parse_boot_argn("ace5_gpu_step_delay",&a750_gpu_step_delay,sizeof(a750_gpu_step_delay));
    if (a750_gpu_step_delay>3000) a750_gpu_step_delay=3000;
    a750_log("ace5-gpu: configure Gen7_9_1 command processors\n");
    const uint32_t pairs[][2]={
        {GEN7_GBIF_QSB_SIDE0,0x71620}, {GEN7_GBIF_QSB_SIDE1,0x71620},
        {GEN7_GBIF_QSB_SIDE2,0x71620}, {GEN7_GBIF_QSB_SIDE3,0x71620},
        {GEN7_RBBM_GBIF_CLIENT_QOS_CNTL,0x2120212},
        {GEN7_UCHE_GBIF_GX_CONFIG,0x10240e0}, {GEN7_RBBM_PERFCTR_GPU_BUSY_MASKED,0xffffffff},
        {GEN7_UCHE_TRAP_BASE_LO,0xfffff000}, {GEN7_UCHE_TRAP_BASE_HI,0x1ffff},
        {GEN7_UCHE_WRITE_THRU_BASE_LO,0xfffff000}, {GEN7_UCHE_WRITE_THRU_BASE_HI,0x1ffff},
        {GEN7_UCHE_CACHE_WAYS,0x800000}, {GEN7_UCHE_CMDQ_CONFIG,0x6690e},
        {GEN7_UCHE_GMEM_RANGE_MIN_LO,0x1000000}, {GEN7_UCHE_GMEM_RANGE_MIN_HI,0},
        {GEN7_UCHE_GMEM_RANGE_MAX_LO,0x12fffff}, {GEN7_UCHE_GMEM_RANGE_MAX_HI,0},
        {GEN7_CP_AHB_CNTL,1}, {0x500,1}, /* RBBM_PERFCTR_CNTL, Linux A7xx hw_init */
        {GEN7_GMU_CX_GMU_POWER_COUNTER_SELECT_1,4},
        {GEN7_RB_NC_MODE_CNTL,0x816}, {GEN7_TPL1_NC_MODE_CNTL,6},
        {GEN7_TPL1_BICUBIC_WEIGHTS_TABLE_1,0x3fe05ff4},
        {GEN7_TPL1_BICUBIC_WEIGHTS_TABLE_2,0x3fa0ebee},
        {GEN7_TPL1_BICUBIC_WEIGHTS_TABLE_3,0x3f5193ed},
        {GEN7_TPL1_BICUBIC_WEIGHTS_TABLE_4,0x3f0243f0},
        {GEN7_SP_NC_MODE_CNTL,0x26}, {GEN7_GRAS_NC_MODE_CNTL,0x60},
        {0x534,1}, /* RBBM_NC_MODE_CNTL: SM8650 macrotile mode, matches Mesa parameter */
        {GEN7_UCHE_MODE_CNTL,0x600000}, {GEN7_RBBM_INTERFACE_HANG_INT_CNTL,0x40cfffff},
        {GEN7_UCHE_CLIENT_PF,0x81}, {GEN7_TPL1_DBG_ECO_CNTL1,0xc0700},
        {GEN7_GPU_GMU_AO_GPU_CX_BUSY_MASK,0xff000000},
        {GEN7_GMU_CX_GMU_POWER_COUNTER_SELECT_0,0x20}, {GEN7_GMU_CX_GMU_POWER_COUNTER_ENABLE,1},
        {GEN7_CP_PROTECT_CNTL,0xb}, {GEN7_CP_APRIV_CNTL,0x6f},
        {GEN7_CP_BV_APRIV_CNTL,0xf}, {GEN7_CP_LPAC_APRIV_CNTL,0xf}, {GEN7_CP_AQE_APRIV_CNTL,1},
        {GEN7_RBBM_SECVID_TSB_CNTL,0}, {GEN7_RBBM_SECVID_TSB_TRUSTED_BASE_LO,0xf0000000},
        {GEN7_RBBM_SECVID_TSB_TRUSTED_BASE_HI,0}, {GEN7_RBBM_SECVID_TSB_TRUSTED_SIZE,0xfffff000},
        {GEN7_GPU_GMU_AO_GMU_CGC_MODE_CNTL,0x20202},
        {GEN7_GPU_GMU_AO_GMU_CGC_DELAY_CNTL,0x10111}, {GEN7_GPU_GMU_AO_GMU_CGC_HYST_CNTL,0x5555},
    };
    for (unsigned i=0;i<sizeof(pairs)/sizeof(pairs[0]);i++)
        a750_gpu_write(gx,gmu,pairs[i][0],pairs[i][1]);
    a750_gpu_step("rmw",GEN7_RB_CMP_DBG_ECO_CNTL,1<<19);
    a750_bits(gx,GEN7_RB_CMP_DBG_ECO_CNTL*4,1<<19,1<<19);
    a750_gpu_step("rmw",GEN7_GBIF_CX_CONFIG,1u<<29);
    a750_bits(gx,GEN7_GBIF_CX_CONFIG*4,7u<<29,1u<<29);
    for (unsigned i=0;i<48;i++) {
        const uint32_t *p=a750_protected[i];
        a750_gpu_write(gx,gmu,GEN7_CP_PROTECT_REG+i,p[0]|((p[1]-p[0])<<18)|(p[2]<<31));
    }
    a750_gpu_write(gx,gmu,GEN7_RBBM_CLOCK_CNTL_GLOBAL,1);
    a750_gpu_write(gx,gmu,GEN7_RBBM_CGC_GLOBAL_LOAD_CMD,1);
    a750_gpu_write(gx,gmu,GEN7_RBBM_CGC_P2S_TRIG_CMD,1);
    if (!a750_poll(gx,GEN7_RBBM_CGC_P2S_STATUS*4,1,1)) return false;
    a750_gpu_write(gx,gmu,GEN7_RBBM_CLOCK_CNTL_GLOBAL,0);
    /* Exact vendor layout: three lock DWORDs and packed IFPC/preemption counts,
     * followed by register/value pairs. IFPC and preemption remain disabled. */
    uint32_t *list=(uint32_t *)a750_find_memory(&s->gx->memory,0x61030000,4096);
    if (!list) return false;
    list[3]=sizeof(a750_ifpc_regs)/sizeof(uint32_t)|
        ((sizeof(a750_preempt_regs)/sizeof(uint32_t))<<8);
    unsigned at=4;
    for (unsigned i=0;i<sizeof(a750_ifpc_regs)/sizeof(uint32_t);i++) {
        a750_gpu_step("read",a750_ifpc_regs[i],0);
        list[at++]=a750_ifpc_regs[i]; list[at++]=a750_rd(gx,a750_ifpc_regs[i]*4);
    }
    for (unsigned i=0;i<sizeof(a750_preempt_regs)/sizeof(uint32_t);i++) {
        a750_gpu_step("read",a750_preempt_regs[i],0);
        list[at++]=a750_preempt_regs[i]; list[at++]=a750_rd(gx,a750_preempt_regs[i]*4);
    }
    a750_flush((vm_offset_t)list,4096);
    a750_gpu_write(gx,gmu,GEN7_CP_SQE_INSTR_BASE_LO,0x61000000);
    a750_gpu_write(gx,gmu,GEN7_CP_SQE_INSTR_BASE_HI,0);
    if (s->direct) return a750_direct_start(s,gx);
    a750_gpu_write(gx,gmu,GEN7_CP_AQE_INSTR_BASE_LO_0,0x61020000);
    a750_gpu_write(gx,gmu,GEN7_CP_AQE_INSTR_BASE_HI_0,0);
    uint32_t cmds[]={0,a750_pm4(0x17,1),1u<<27,
        a750_pm4(0x48,7),0x94b,3,0x20000000,2,0x61030000,0,1u<<31};
    a750_log("ace5-gpu: issue CP_ME_INIT through HFI raw PM4\n");
    a750_gpu_step("submit",0x48,0x94b);
    bool good=a750_hfi_send(s,131,cmds,11,false,NULL);
    good=a750_gpu_idle(s,gx) && good;
    a750_log("ace5-gpu: stage 17 %s; SQE initialization and CP idle/error checks, AQE disabled\n",good?"complete":"FAILED");
    return good;
}
