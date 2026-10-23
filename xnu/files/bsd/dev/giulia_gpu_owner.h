/* The sleeping owner retains all bring-up locals through service lifetime. */
static LCK_GRP_DECLARE(a750_live_group,"ace5-gpu");
static LCK_MTX_DECLARE(a750_live_lock,&a750_live_group);
static void a750_live_stop(void)
{
    lck_mtx_lock(&a750_live_lock);
    if (a750_live_phase==1) { a750_live_phase=2;thread_wakeup((event_t)&a750_live_phase); }
    uint64_t deadline=0;clock_interval_to_deadline(10,NSEC_PER_SEC,&deadline);
    while (a750_live_phase!=3)
        if (lck_mtx_sleep_deadline(&a750_live_lock,LCK_SLEEP_DEFAULT,(event_t)&a750_live_phase,THREAD_UNINT,deadline)==THREAD_TIMED_OUT)
            panic("ace5-gpu: owner shutdown timed out; retain memory");
    lck_mtx_unlock(&a750_live_lock);
}
static bool a750_keep_wait(struct a750_hfi *s,vm_offset_t gx,unsigned wptr,uint32_t fence)
{
    a750_live.session=s;a750_live.gx=gx;a750_live.wptr=wptr;a750_live.fence=fence;
    a750_live.ring=(uint32_t *)a750_find_memory(&s->gx->memory,0x61040000,0x8000);
    a750_live.memptrs=(volatile uint32_t *)a750_find_memory(&s->gx->memory,0x61048000,4096);
    a750_gpu_step_delay=0;
    a750_runtime_active=true;
    redwood_gpu_use_backend(&a750_backend);
    a750_log("ace5-gpu: persistent A750 backend published; owner asleep, GPU at 231 MHz\n");
    lck_mtx_lock(&a750_live_lock);a750_live_phase=1;
    thread_wakeup((event_t)&a750_live_phase);
    while (a750_live_phase==1)
        lck_mtx_sleep(&a750_live_lock,LCK_SLEEP_DEFAULT,(event_t)&a750_live_phase,THREAD_UNINT);
    lck_mtx_unlock(&a750_live_lock);
    a750_log("ace5-gpu: stop requested; drain user commands before full teardown\n");
    bool good=a750_direct_wait(s,gx,a750_live.memptrs,a750_live.wptr,a750_live.fence);
    return a750_gpu_idle(s,gx) && good;
}
static bool a750_probe(void);
static bool a750_board(void)
{
    DTEntry entry;const void *prop;unsigned size;
    if (SecureDTLookupEntry(NULL,"/arm-io/gic",&entry)!=kSuccess ||
        SecureDTGetProperty(entry,"reg",&prop,&size)!=kSuccess || size<4*sizeof(uint64_t)) return false;
    const uint64_t *reg=prop;
    return pe_arm_get_soc_base_phys()+reg[0]==0x17100000 && pe_arm_get_soc_base_phys()+reg[2]==0x17180000;
}
static void a750_owner(void *arg,wait_result_t result)
{
    (void)arg;(void)result;
    a750_probe();
    lck_mtx_lock(&a750_live_lock);
    a750_live.session=NULL;a750_live_phase=3;
    thread_wakeup((event_t)&a750_live_phase);
    lck_mtx_unlock(&a750_live_lock);
    thread_terminate(current_thread());
}
bool giulia_adreno_init(void)
{
    if (!a750_board()) return false;
    PE_parse_boot_argn("ace5_gpu_keep",&a750_gpu_keep,sizeof(a750_gpu_keep));
    if (!a750_gpu_keep) return a750_probe();
    thread_t thread;
    if (kernel_thread_start(a750_owner,NULL,&thread)!=KERN_SUCCESS) return true;
    thread_deallocate(thread);
    lck_mtx_lock(&a750_live_lock);
    uint64_t deadline=0;clock_interval_to_deadline(30,NSEC_PER_SEC,&deadline);
    while (!a750_live_phase)
        if (lck_mtx_sleep_deadline(&a750_live_lock,LCK_SLEEP_DEFAULT,(event_t)&a750_live_phase,THREAD_UNINT,deadline)==THREAD_TIMED_OUT)
            panic("ace5-gpu: owner startup timed out; retain memory");
    bool ready=a750_live_phase==1;
    lck_mtx_unlock(&a750_live_lock);
    a750_log("ace5-gpu: persistent startup ready=%u\n",ready);
    return true;
}
