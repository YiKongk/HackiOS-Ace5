/* SM8650 gen7_2_x-family RSC/PDC sequence from the matching OnePlus driver. */
struct a750_power {
    vm_offset_t rsc, pdc;
    uint32_t saved[17], pdc_start, pdc_enable, rsc_gate;
    bool configured;
};
static const uint32_t a750_rsc_words[]={0xb,0xe,0xd,0x10,0xf,0x12,0x11,
    0x100,8,9,10,0x154,0x155,0x156,0x157,0x158};
static bool a750_power_prepare(struct a750_power *s, vm_offset_t gmu)
{
    s->rsc=ml_io_map(0x3d50000,0x10000);
    s->pdc=ml_io_map(0xb280000,0x10000);
    if (!s->rsc || !s->pdc) return false;
    const uint32_t values[]={1,0,0,0,0,0x80000021,0,0,0x4520,0x4510,0x4514,
        0xeaaae5a0,0xe1a1ebab,0xa2e0a581,0xecac82e2,0x0020edad};
    a750_log("ace5-gpu: configure RSC/PDC sleep sequencer\n");
    uint32_t status=a750_rd(s->rsc,0x10);
    s->rsc_gate=status&(1<<24);
    s->pdc_start=a750_rd(s->pdc,0x4520); s->pdc_enable=a750_rd(s->pdc,0x4500);
    a750_wr(s->rsc,0x10,1<<24);
    for (unsigned i=0;i<sizeof(a750_rsc_words)/sizeof(a750_rsc_words[0]);i++) {
        s->saved[i]=a750_rd(s->rsc,a750_rsc_words[i]*4);
        a750_wr(s->rsc,a750_rsc_words[i]*4,values[i]);
    }
    a750_wr(s->pdc,0x4520,0); a750_wr(s->pdc,0x4500,0x80000001);
    s->configured=true;
    a750_read(s->rsc,0x10,"RSC_STATUS_CONFIGURED");
    /* RSC retains its sleep state across CX collapse and another RAM kernel.
     * Releasing CM3 without this handshake leaves RPMh requests stalled. */
    if (status&(1<<16)) {
        a750_log("ace5-gpu: waking retained RSC sleep state\n");
        a750_wr(gmu,0x26c1c,2);
        bool good=a750_poll(gmu,0x26c20,2,2) && a750_poll(s->rsc,0x404,0xffffffff,0);
        a750_wr(gmu,0x26c1c,0);
        a750_read(s->rsc,0x10,"RSC_STATUS_AWAKE");
        if (!good) return false;
    }
    return true;
}
static bool a750_power_sleep(struct a750_power *s, vm_offset_t gmu)
{
    /* CM3 must already be held in reset. Keep its mappings until the RSC
     * sequencer confirms its RPMh sleep votes have completed. */
    if (!s->configured || !(a750_rd(gmu,0x16000)&1)) return false;
    a750_wr(gmu,0x26c1c,1);
    bool done=false;
    for (unsigned i=0;i<1000;i++) {
        if (a750_rd(s->rsc,0x10)&(1<<16)) { done=true; break; }
        delay_for_interval(100,NSEC_PER_USEC);
    }
    a750_read(s->rsc,0x10,"RSC_STATUS_SLEEP");
    a750_wr(gmu,0x26c1c,0);
    return done;
}
static void a750_power_restore(struct a750_power *s)
{
    if (!s->configured) return;
    a750_wr(s->pdc,0x4500,0);
    for (unsigned i=0;i<sizeof(a750_rsc_words)/sizeof(a750_rsc_words[0]);i++)
        a750_wr(s->rsc,a750_rsc_words[i]*4,s->saved[i]);
    a750_wr(s->rsc,0x10,s->rsc_gate);
    a750_wr(s->pdc,0x4520,s->pdc_start); a750_wr(s->pdc,0x4500,s->pdc_enable);
    s->configured=false;
}
