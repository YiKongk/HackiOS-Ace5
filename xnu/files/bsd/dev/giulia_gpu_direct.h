/* Bounded host CP ring diagnostic, following Linux v6.17 a7xx_cp_init and
 * UniXNU NativeAdreno's CP initialization / CACHE_CLEAN fence sequence.
 * This proves DMA only; it does not publish a rendering device. */
static bool a750_direct_wait(struct a750_hfi *s, vm_offset_t gx,
    volatile uint32_t *memptrs, unsigned wptr, uint32_t fence)
{
    bool done=false;
    for (unsigned i=0;i<2000;i++) {
        a750_flush((vm_offset_t)memptrs,64);
        if (a750_gx_faulted(s) || (a750_rd(s->gmu,0x16648)&0xe00)) break;
        if (memptrs[1]==fence && a750_rd(gx,0x806*4)==wptr) { done=true; break; }
        delay_for_interval(100,NSEC_PER_USEC);
    }
    a750_log("ace5-gpu: direct fence expected=%u actual=%u RPTR=%u shadow=%u BV=%u WPTR=%u done=%u\n",
        fence,memptrs[1],a750_rd(gx,0x806*4),memptrs[0],memptrs[2],a750_rd(gx,0x807*4),done);
    return done;
}
static unsigned a750_direct_fence(uint32_t *ring, unsigned at, uint32_t value)
{
    ring[at++]=a750_pm4(0x46,4); /* CP_EVENT_WRITE7 */
    ring[at++]=0x31|(1u<<27);    /* CACHE_CLEAN, write timestamp, no IRQ */
    ring[at++]=0x61048004;
    ring[at++]=0;
    ring[at++]=value;
    return at;
}
static bool a750_direct_start(struct a750_hfi *s, vm_offset_t gx)
{
    uint32_t *ring=(uint32_t *)a750_find_memory(&s->gx->memory,0x61040000,0x8000);
    volatile uint32_t *memptrs=(volatile uint32_t *)a750_find_memory(&s->gx->memory,0x61048000,4096);
    volatile uint32_t *scratch=(volatile uint32_t *)a750_find_memory(&s->gx->memory,0x61049000,4096);
    if (!ring || !memptrs || !scratch) return false;
    if (a750_rd(gx,0x806*4) || a750_rd(gx,0x807*4) || memptrs[1] || scratch[0]) {
        a750_log("ace5-gpu: direct ring is not fresh; refusing submission\n");
        return false;
    }
    const uint32_t regs[][2]={
        {0x800,0x61040000},{0x801,0},{0x802,0x20c},
        {0x804,0x61048000},{0x805,0},{0xa98,0x61048008},{0xa99,0},
    };
    for (unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);i++) {
        a750_gpu_write(gx,s->gmu,regs[i][0],regs[i][1]);
        uint32_t value=a750_rd(gx,regs[i][0]*4);
        a750_log("ace5-gpu: direct register %#x read=%#x\n",regs[i][0],value);
        if (value!=regs[i][1]) return false;
    }
    /* Vendor Gen7 adds bit 11 to disable DMS. Keep it disabled because this
     * diagnostic has no DMS resources; the remaining fields match a7xx. */
    const uint32_t init[]={a750_pm4(0x17,1),1u<<27,a750_pm4(0x48,7),
        0x94b,3,0x20000000,2,0x61030000,0,1u<<31};
    memcpy(ring,init,sizeof(init));
    unsigned wptr=a750_direct_fence(ring,sizeof(init)/sizeof(init[0]),1);
    a750_flush((vm_offset_t)ring,0x8000);
    a750_log("ace5-gpu: direct CP_ME_INIT prepared, %u DWORDs\n",wptr);
    a750_gpu_write(gx,s->gmu,0x808,1); /* release SQE halt */
    a750_gpu_step("SQE running",0x808,a750_rd(gx,0x808*4));
    a750_gpu_write(gx,s->gmu,0x807,wptr);
    a750_gpu_step("direct published",0x807,wptr);
    bool good=a750_direct_wait(s,gx,memptrs,wptr,1);
    if (good) {
        ring[wptr++]=a750_pm4(0x3d,3); /* CP_MEM_WRITE */
        ring[wptr++]=0x61049000; ring[wptr++]=0; ring[wptr++]=0xa7500001;
        wptr=a750_direct_fence(ring,wptr,2);
        a750_flush((vm_offset_t)ring,0x8000);
        a750_gpu_write(gx,s->gmu,0x807,wptr);
        a750_gpu_step("DMA published",0x807,wptr);
        good=a750_direct_wait(s,gx,memptrs,wptr,2);
        a750_flush((vm_offset_t)scratch,64);
        a750_log("ace5-gpu: direct GPU DMA scratch=%#x expected=0xa7500001\n",scratch[0]);
        good=good && scratch[0]==0xa7500001;
    }
    good=a750_gpu_idle(s,gx) && good;
    a750_log("ace5-gpu: stage 17 %s; direct CP ring, GPU memory write and two fences\n",good?"complete":"FAILED");
    if (good && s->stage>=18) {
        good=a750_zap_load(s);
        a750_log("ace5-gpu: stage 18 %s; PAS13 ZAP authentication\n",good?"complete":"FAILED");
    }
    if (good && s->stage>=19) {
        ring[wptr++]=a750_pm4(0x66,1); /* CP_SET_SECURE_MODE */
        ring[wptr++]=0;
        wptr=a750_direct_fence(ring,wptr,3);
        a750_flush((vm_offset_t)ring,0x8000);
        a750_gpu_write(gx,s->gmu,0x807,wptr);
        a750_gpu_step("nonsecure published",0x807,wptr);
        good=a750_direct_wait(s,gx,memptrs,wptr,3);
        if (good) {
            ring[wptr++]=a750_pm4(0x3d,3);
            ring[wptr++]=0x61049004; ring[wptr++]=0; ring[wptr++]=0xa7500019;
            wptr=a750_direct_fence(ring,wptr,4);
            a750_flush((vm_offset_t)ring,0x8000);
            a750_gpu_write(gx,s->gmu,0x807,wptr);
            good=a750_direct_wait(s,gx,memptrs,wptr,4);
            a750_flush((vm_offset_t)scratch,64);
            a750_log("ace5-gpu: nonsecure GPU DMA scratch[1]=%#x expected=0xa7500019\n",scratch[1]);
            good=good && scratch[1]==0xa7500019;
        }
        good=a750_gpu_idle(s,gx) && good;
        a750_log("ace5-gpu: stage 19 %s; nonsecure mode and GPU DMA\n",good?"complete":"FAILED");
    }
    if (good && s->stage>=20) {
        /* The IB has independent writes on both CP threads. It shares the
         * owned scratch page, but not the cache lines holding DMA results. */
        uint32_t *ib=(uint32_t *)scratch+64;
        const uint32_t cmds[]={a750_pm4(0x17,1),1,a750_pm4(0x3d,3),0x61049008,0,0xa75000b1,
            a750_pm4(0x17,1),2,a750_pm4(0x3d,3),0x6104900c,0,0xa75000b2,a750_pm4(0x17,1),3};
        memcpy(ib,cmds,sizeof(cmds));
        a750_flush((vm_offset_t)ib,sizeof(cmds));
        uint64_t address=0x61049100;uint32_t size=sizeof(cmds)/sizeof(cmds[0]);
        unsigned n=a750_submit_words(ring+wptr,8192-wptr,&address,&size,1,4,5,true);
        good=n!=0;
        if (good) {
            wptr+=n;
            a750_flush((vm_offset_t)ring,0x8000);
            a750_log("ace5-gpu: indirect BR/BV test prepared, %u DWORDs\n",n);
            a750_gpu_write(gx,s->gmu,0x807,wptr);
            a750_gpu_step("IB published",0x807,wptr);
            good=a750_direct_wait(s,gx,memptrs,wptr,5);
            a750_flush((vm_offset_t)scratch,64);
            a750_log("ace5-gpu: indirect DMA BR=%#x BV=%#x BV-fence=%u\n",scratch[2],scratch[3],memptrs[3]);
            good=good && scratch[2]==0xa75000b1 && scratch[3]==0xa75000b2 && memptrs[3]==5;
            good=a750_gpu_idle(s,gx) && good;
        }
        a750_log("ace5-gpu: stage 20 %s; indirect buffer, BR/BV writes and joined fences\n",good?"complete":"FAILED");
    }
    if (good && a750_gpu_keep) good=a750_keep_wait(s,gx,wptr,5);
    a750_gpu_write(gx,s->gmu,0x808,3); /* bounded probe ends; halt before slumber */
    good=a750_zap_shutdown(s) && good;
    return good;
}
