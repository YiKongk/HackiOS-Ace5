/* Keep the validated bring-up stack on a sleeping owner thread. Its saved
 * power/clock state and owned memory remain alive until RedwoodGPUStop wakes
 * it to execute the same tested shutdown path. Client calls are serialized by
 * RedwoodGPU's lock; this lock only coordinates owner startup/shutdown. */
static unsigned a750_live_phase; /* 0 starting, 1 ready, 2 stop requested, 3 stopped */
static struct {
    struct a750_hfi *session;
    vm_offset_t gx;
    uint32_t *ring;
    volatile uint32_t *memptrs;
    unsigned wptr;
    uint32_t fence;
    bool failed;
} a750_live;
#define A750_USER_VA_START 0x70000000ull
#define A750_USER_VA_END   0xff000000ull
extern uint32_t gpu_log, gpu_va;
static bool a750_live_parameter(uint32_t which,uint64_t *value)
{
    switch (which) {
    case 1:case 8:case 9:case 10:case 17:*value=0;return true;
    case 2:*value=3*1024*1024;return true;
    case 3:*value=0x443051401ull;return true; /* stock speed bin 4, A750v2 */
    case 4:*value=231000000;return true; /* fixed diagnostic operating point */
    case 5:*value=mach_absolute_time();return true;
    case 6:*value=0x1000000;return true;
    case 7:*value=1;return true;
    case 16:*value=16;return true;
    case 18:*value=6;return true;
    case 19:*value=1;return true;
    case 14:case 15:
        if (!gpu_va) return false;
        *value=which==14?A750_USER_VA_START:A750_USER_VA_END-A750_USER_VA_START;return true;
    default:return false;
    }
}
static bool a750_live_map(uint64_t address,uint64_t phys,bool readonly)
{
    if (!a750_live.session || a750_live.failed || address<A750_USER_VA_START || address>=A750_USER_VA_END ||
        ((address|phys)&4095) || !phys || phys>>36) return false;
    uint64_t *pte=a750_leaf(&a750_live.session->gx->memory,(uint32_t)address,true);
    if (!pte || *pte) return false;
    *pte=phys|3|(2<<8)|(1<<10)|(1<<11)|(1<<6)|(readonly?1<<7:0);
    a750_flush((vm_offset_t)pte,sizeof(*pte));
    return true;
}
static void a750_live_mapped(void)
{
    if (!a750_live.session) return;
    vm_offset_t smmu=a750_live.session->smmu;
    a750_wr(smmu,0x20610,0);a750_wr(smmu,0x207f0,0xffffffff);
    if (!a750_poll(smmu,0x207f4,1,0)) panic("ace5-gpu: live GPU TLB synchronization failed");
}
static void a750_live_unmap(uint64_t address,uint64_t size)
{
    if (!a750_live.session) return;
    if (address<A750_USER_VA_START || address>=A750_USER_VA_END || (address&4095) || (size&4095) || size>A750_USER_VA_END-address)
        panic("ace5-gpu: invalid live GPU unmap range");
    for (uint64_t off=0;off<size;off+=4096) {
        uint64_t *pte=a750_leaf(&a750_live.session->gx->memory,(uint32_t)(address+off),false);
        if (pte) { *pte=0;a750_flush((vm_offset_t)pte,sizeof(*pte)); }
    }
    a750_live_mapped();
}
static bool a750_live_done(uint32_t fence)
{
    if (!fence) return true;
    if (!a750_live.session || a750_live.failed) return false;
    a750_flush((vm_offset_t)a750_live.memptrs,64);
    return (int32_t)(a750_live.memptrs[1]-fence)>=0;
}
static uint32_t a750_live_last(void) { return a750_live.fence; }
static void a750_live_report(const char *why)
{
    if (!a750_live.session) return;
    a750_log("ace5-gpu: live failure %s, fence=%u completed=%u RPTR=%u WPTR=%u\n",why,
        a750_live.fence,a750_live.memptrs[1],a750_rd(a750_live.gx,0x806*4),a750_live.wptr);
    a750_gx_faulted(a750_live.session);
    a750_gpu_idle(a750_live.session,a750_live.gx);
    a750_live.failed=true;
}
static uint32_t a750_live_submit(const uint64_t *addresses,const uint32_t *words,uint32_t count)
{
    if (!a750_live.session || a750_live.failed || !addresses || !words || !count || count>128) return 0;
    for (unsigned i=0;i<count;i++)
        if (addresses[i]<A750_USER_VA_START || addresses[i]>=A750_USER_VA_END ||
            (uint64_t)words[i]*4>A750_USER_VA_END-addresses[i]) return 0;
    uint32_t fence=a750_live.fence+1;
    if (!fence) fence=1;
    uint32_t packet[580];
    /* No private page tables yet: reset context state on every client submit. */
    unsigned n=a750_submit_words(packet,580,addresses,words,count,a750_live.fence,fence,true);
    if (!n) return 0;
    unsigned room=0;
    for (unsigned i=0;i<5000;i++) {
        unsigned rptr=a750_rd(a750_live.gx,0x806*4);
        if (rptr>=8192 || a750_gx_faulted(a750_live.session)) { a750_live_report("ring fault");return 0; }
        room=(rptr-a750_live.wptr-1)&8191;
        if (room>=n) break;
        delay_for_interval(100,NSEC_PER_USEC);
    }
    if (room<n) { a750_live_report("ring full");return 0; }
    for (unsigned i=0;i<n;i++) a750_live.ring[(a750_live.wptr+i)&8191]=packet[i];
    a750_flush((vm_offset_t)a750_live.ring,0x8000);
    a750_live.wptr=(a750_live.wptr+n)&8191;a750_live.fence=fence;
    a750_wr(a750_live.gx,0x807*4,a750_live.wptr);
    if (gpu_log || fence<=10)
        a750_log("ace5-gpu: user submission fence=%u IBs=%u words=%u WPTR=%u\n",fence,count,n,a750_live.wptr);
    return fence;
}
static void a750_live_quiesce(void)
{
    if (!a750_live.session || !a750_runtime_active) return;
    if (!(a750_rd(a750_live.session->gmu,0x16340)&1)) a750_wr(a750_live.gx,0x808*4,3);
    a750_wr(a750_live.session->gmu,0x16000,1);
    a750_wr(a750_live.gx,0xf114,2);
}
static void a750_live_stop(void);
static const struct redwood_gpu_backend a750_backend={
    A750_USER_VA_START,A750_USER_VA_END-A750_USER_VA_START,a750_live_parameter,
    a750_live_map,a750_live_mapped,a750_live_unmap,a750_live_submit,a750_live_done,
    a750_live_last,a750_live_report,a750_live_stop,a750_live_quiesce
};
