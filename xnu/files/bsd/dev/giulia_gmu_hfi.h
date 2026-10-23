/* Gen7 HFI v2. Queue memory is shared with the CM3 through an uncached alias. */
struct a750_hfi {
    volatile uint32_t *h;
    vm_offset_t gmu, smmu;
    unsigned sequence;
    struct a750_memory *memory;
    uint32_t next_va;
    uint32_t allocations[48][9];
    unsigned allocation_count;
    struct a750_gx *gx;
    bool direct;
    unsigned stage;
};
static bool a750_gx_prepare(struct a750_hfi *);
static void a750_gx_release(struct a750_hfi *);
static bool a750_gx_start(struct a750_hfi *);
static bool a750_gx_faulted(struct a750_hfi *);
static void a750_gpu_step(const char *, uint32_t, uint32_t);
static bool a750_keep_wait(struct a750_hfi *, vm_offset_t, unsigned, uint32_t);
static void a750_hfi_diagnose(struct a750_hfi *s)
{
    const uint32_t regs[]={0x16070,0x16068,0x16648,0x26c14,0x16340,0x163b0,0x16738};
    for (unsigned i=0;i<sizeof(regs)/sizeof(regs[0]);i++)
        a750_log("ace5-gpu: HFI fault GMU +%#x=%#x\n",regs[i],a750_rd(s->gmu,regs[i]));
    a750_log("ace5-gpu: HFI fault SMMU FSR=%#x FAR=%#x:%#x\n",a750_rd(s->smmu,0x22058),
        a750_rd(s->smmu,0x22064),a750_rd(s->smmu,0x22060));
    volatile uint32_t *q=s->h+30, *data=s->h+212+2048;
    unsigned r=q[10],w=q[11];
    a750_log("ace5-gpu: HFI debug queue r/w=%u/%u\n",r,w);
    if (r<1024 && w<1024) for (unsigned i=0;i<((w-r)&1023) && i<64;i++)
        a750_log("ace5-gpu: HFI debug[%u]=%#x\n",i,data[(r+i)&1023]);
    vm_offset_t log=a750_find_memory(s->memory,0x60002000,0x10000);
    if (log) {
        a750_flush(log,0x10000);
        const uint32_t *p=(const uint32_t *)log;
        unsigned lines=0;
        for (unsigned i=0;i<0x4000 && lines<96;i+=4) if (p[i]|p[i+1]|p[i+2]|p[i+3]) {
            a750_log("ace5-gpu: GMU log[%#x]=%08x %08x %08x %08x\n",i*4,p[i],p[i+1],p[i+2],p[i+3]); lines++;
        }
    }
}
static bool a750_hfi_write(struct a750_hfi *s, unsigned id, unsigned type,
    uint32_t *p, unsigned words)
{
    if (words<1 || words>255) return false;
    if (id==131) a750_gpu_step("HFI queue access",id,words);
    volatile uint32_t *cmd=s->h+6, *cmdq=s->h+212;
    unsigned r=cmd[10], w=cmd[11], padded=(words+3)&~3u;
    if (r>=1024 || w>=1024 || ((r-w-1)&1023)<padded) return false;
    p[0]=((++s->sequence&4095)<<20)|(type<<16)|(words<<8)|id;
    for (unsigned i=0;i<padded;i++) cmdq[(w+i)&1023]=i<words?p[i]:0xfafafafa;
    __asm__ volatile("dsb sy":::"memory");
    if (id==131) {
        a750_log("ace5-gpu: raw command prepared r/w=%u/%u header=%#x\n",r,w,p[0]);
        for (unsigned i=1;i<words;i++) a750_log("ace5-gpu: raw command[%u]=%#x\n",i,p[i]);
        /* Firmware may poll the queue without waiting for the doorbell. Keep
         * this breadcrumb before publishing the new write index. */
        a750_gpu_step("HFI publish",id,(w+padded)&1023);
    }
    cmd[11]=(w+padded)&1023;
    __asm__ volatile("dsb sy":::"memory");
    a750_log("ace5-gpu: HFI send id=%u words=%u seq=%u\n",id,words,s->sequence);
    if (id==131) a750_gpu_step("HFI doorbell",0x16650,1);
    a750_wr(s->gmu,0x16650,1);
    if (id==131) a750_gpu_step("HFI notified",id,p[0]);
    return true;
}
static bool a750_hfi_allocate(struct a750_hfi *s, const uint32_t *p, unsigned count)
{
    if (count<10 || p[1]>1 || (p[1] && count<11)) return false;
    uint32_t reply[11]={0,p[0]}, *d=reply+2;
    memcpy(d,p+2,(p[1]?9:8)*sizeof(uint32_t));
    unsigned flags=d[2], kind=d[3], bytes=d[7], align=d[8];
    a750_log("ace5-gpu: HFI allocation kind=%u flags=%#x size=%#x align=%#x handle=%u\n",
        kind,flags,bytes,align,d[5]);
    for (unsigned i=0;i<s->allocation_count;i++) {
        if (s->allocations[i][3]==kind && s->allocations[i][5]==d[5]) {
            memcpy(d,s->allocations[i],9*sizeof(uint32_t));
            return a750_hfi_write(s,20,1,reply,11);
        }
    }
    /* Secure allocations and MMIO require separate ownership procedures.
     * Optional profiling/fences/preemption are not enabled in this phase. */
    if (!bytes || bytes>1024*1024 || flags&~0x37f ||
        kind>=26 || (kind>=11 && kind<=13) || kind==19 || kind==20 || kind==23 ||
        (align&~0xffffu) || (align&255)>20 || ((align>>8)&255)>20 ||
        s->allocation_count==48) return false;
    unsigned va_align=1u<<((align&255)>12?(align&255):12);
    unsigned sz_align=1u<<(((align>>8)&255)>12?((align>>8)&255):12);
    uint32_t va=(s->next_va+va_align-1)&~(va_align-1);
    uint32_t size=(bytes+sz_align-1)&~(sz_align-1);
    if (va<0x60040000 || (uint64_t)va+size>0x64000000 ||
        !a750_block(s->memory,va,size,false)) return false;
    vm_offset_t backing=a750_find_memory(s->memory,va,size);
    a750_flush(backing,size);
    for (unsigned i=0;i<s->memory->tables;i++) a750_flush(s->memory->table[i],4096);
    a750_wr(s->smmu,0x22610,2); a750_wr(s->smmu,0x227f0,0xffffffff);
    if (!a750_poll(s->smmu,0x227f4,1,0)) return false;
    s->next_va=va+size;
    /* Reserve the same VA for GX; its context bank is enabled separately,
     * before OOB GPU power-on and command processor startup. */
    d[0]=(flags&1)?va:0; d[1]=0; d[4]=d[5]; d[6]=(flags&8)?va:0; d[7]=size;
    memcpy(s->allocations[s->allocation_count++],d,9*sizeof(uint32_t));
    return a750_hfi_write(s,20,1,reply,11);
}
static bool a750_hfi_send(struct a750_hfi *s, unsigned id, uint32_t *p,
    unsigned words, bool query, uint32_t *value)
{
    if (!a750_hfi_write(s,id,0,p,words)) return false;
    uint32_t header=p[0];
    volatile uint32_t *cmd=s->h+6, *msg=s->h+18, *msgq=s->h+212+1024;
    for (unsigned wait=0;wait<5000;wait++) {
        if (id==131 && s->gx && a750_gx_faulted(s)) {
            a750_hfi_diagnose(s);
            return false;
        }
        unsigned r=msg[10], w=msg[11];
        if (r>=1024 || w>=1024) return false;
        if (r!=w) {
            __asm__ volatile("dmb sy":::"memory");
            uint32_t packet[255]={0};
            unsigned count=(msgq[r]>>8)&255, occupied=(count+3)&~3u;
            if (count<2 || occupied>((w-r)&1023)) return false;
            for (unsigned i=0;i<count;i++) packet[i]=msgq[(r+i)&1023];
            msg[10]=(r+occupied)&1023;
            __asm__ volatile("dsb sy":::"memory");
            a750_log("ace5-gpu: HFI rx hdr=%#x request=%#x words=%u data=%#x,%#x\n",
                packet[0],packet[1],count,count>2?packet[2]:0,count>3?packet[3]:0);
            if (((packet[0]>>16)&15)==1 && packet[1]==header) {
                /* GET_VALUE v5 has data followed by status; generic ACKs have
                 * status directly after the echoed request header. */
                unsigned status=query?3:2;
                if (count<=status || packet[status]) return false;
                if (value) *value=packet[2];
                return !(a750_rd(s->smmu,0x22058)&0xc00001fe);
            }
            if (id==10 && ((packet[0]>>16)&15)==0) {
                if ((packet[0]&255)==20 && a750_hfi_allocate(s,packet,count)) continue;
                if ((packet[0]&255)==110 && count==3) {
                    uint32_t reply[]={0,packet[0],packet[1],packet[2],0,0};
                    if (a750_hfi_write(s,110,1,reply,6)) continue;
                }
            }
            a750_log("ace5-gpu: unexpected HFI packet; operation aborted\n");
            return false;
        }
        if ((a750_rd(s->gmu,0x16070)&0xe00) ||
            (a750_rd(s->smmu,0x22058)&0xc00001fe)) break;
        delay_for_interval(100,NSEC_PER_USEC);
    }
    a750_log("ace5-gpu: HFI timeout id=%u cmd=%u/%u msg=%u/%u\n",id,cmd[10],cmd[11],msg[10],msg[11]);
    a750_hfi_diagnose(s);
    return false;
}

/* PKG110 bin 4 / SKU 3, verified against live KGSL frequency list and cmd-db.
 * Values are ARC levels, not voltages. Keep this board-specific table separate
 * from the transport; never reuse it for another device or silicon bin. */
static uint32_t a750_arc_vote(unsigned level, unsigned primary)
{
    static const uint16_t mx[]={0,0x10,0x40,0x80,0xc0,0x100,0x140,0x180,0x1a0,0x1e0};
    unsigned secondary=0;
    while (secondary+1<sizeof(mx)/sizeof(mx[0]) && mx[secondary]<level) secondary++;
    return (level<<16)|(secondary<<8)|primary;
}
static uint32_t a750_bcm_vote(uint32_t bandwidth, uint32_t width, uint32_t buswidth, uint32_t unit)
{
    uint64_t vote=(((uint64_t)bandwidth*width)/buswidth)*1000/unit;
    if (bandwidth && !vote) vote=1;
    if (vote>0x3fff) vote=0x3fff;
    return 0x40000000|(vote?0x20000000:0)|((uint32_t)vote<<14)|(uint32_t)vote;
}
static bool a750_hfi_tables(struct a750_hfi *s)
{
    if (a750_rd(s->gmu,0xcff0)!=0x20060001) {
        a750_log("ace5-gpu: unexpected HFI firmware version; tables refused\n"); return false;
    }
    const uint32_t freq[]={0,231000,310000,366000,422000,500000,578000,
        629000,680000,720000,770000,834000,903000};
    const uint16_t gfx[]={0,0x34,0x38,0x3c,0x40,0x50,0x80,0x90,0xc0,0xe0,0x100,0x140,0x180};
    uint32_t perf[52]={0,0,1,13,3};
    unsigned n=5;
    for (unsigned i=0;i<13;i++) {
        perf[n++]=a750_arc_vote(gfx[i],i); perf[n++]=0xffffffff; perf[n++]=freq[i];
    }
    perf[n++]=3; perf[n++]=2;
    perf[n++]=0; perf[n++]=0;
    perf[n++]=a750_arc_vote(0x40,2); perf[n++]=260000;
    perf[n++]=a750_arc_vote(0x80,3); perf[n++]=625000;
    if (!a750_hfi_send(s,15,perf,n,false,NULL)) return false;
    const uint32_t ddr[]={0,0x209a8e,0x2dc6c0,0x5caf6a,0x65ce03,0x7cb163,
        0xa3140c,0xbdf5c2,0xdbb3e5,0xfbc520};
    uint32_t bw[160]={0,10,1,3,1,7};
    bw[6]=0x50030; /* CN0: unit 140000, width 4, VCD 5 */
    bw[12]=a750_bcm_vote(0,4,4,140000);
    bw[18]=a750_bcm_vote(100,4,4,140000);
    bw[24]=0x50004; bw[25]=0x50000; bw[26]=0x50068;
    for (unsigned i=0;i<10;i++) {
        bw[32+8*i]=a750_bcm_vote(ddr[i],64,16,4800000); /* SH0, VCD1 */
        bw[33+8*i]=a750_bcm_vote(ddr[i],16,4,4700000); /* MC0, VCD0 */
        bw[34+8*i]=0x40000000|(i?0x20000000:0)|(ddr[i]>=10944000?4:0); /* ACV, VCD12 */
    }
    return a750_hfi_send(s,3,bw,160,false,NULL);
}

extern vm_offset_t ml_io_map_wcomb(vm_offset_t, vm_size_t);
static bool a750_hfi_query(struct a750_hfi *s, struct a750_memory *m, vm_offset_t gmu, vm_offset_t smmu, unsigned stage)
{
    vm_offset_t backing=a750_find_memory(m,0x60020000,0x4000);
    if (!backing) return false;
    uint64_t pa=kvtophys(backing);
    for (unsigned i=0;i<0x4000;i+=4096) if (kvtophys(backing+i)!=pa+i) return false;
    *s=(struct a750_hfi){.h=(volatile uint32_t *)ml_io_map_wcomb(pa,0x4000),.gmu=gmu,.smmu=smmu,
        .memory=m,.next_va=0x60040000,.direct=a750_gpu_direct!=0,.stage=stage};
    if (!s->h) return false;
    a750_log("ace5-gpu: starting HFI message queues\n");
    a750_wr(gmu,0x16618,1);
    bool initialized=false;
    for (unsigned i=0;i<1000;i++) {
        if (a750_rd(gmu,0x16600)&1) { initialized=true; break; }
        delay_for_interval(100,NSEC_PER_USEC);
    }
    a750_read(gmu,0x16600,"HFI_CTRL_STATUS");
    if (!initialized) return false;
    uint32_t query[]={0,125,0}, value=0;
    bool good=a750_hfi_send(s,12,query,3,true,&value);
    a750_log("ace5-gpu: stage 11 %s; HFI GET_VALUE round trip, warmboot=%u\n",good?"complete":"FAILED",value);
    if (good && stage>=12) {
        good=a750_hfi_tables(s);
        a750_log("ace5-gpu: stage 12 %s; performance and bandwidth tables acknowledged\n",good?"complete":"FAILED");
    }
    if (good && stage>=13) {
        uint32_t feature[]={0,1,1,0}, core[]={0,0};
        good=(s->direct || a750_hfi_send(s,11,feature,4,false,NULL)) &&
            a750_hfi_send(s,14,core,2,false,NULL);
        a750_read(gmu,0x16340,"GFX_PWR_CLK_AFTER_CORE");
        a750_read(gmu,0x163b0,"RPMH_POWER_STATE_AFTER_CORE");
        a750_log("ace5-gpu: stage 13 %s; %s, CORE_FW_START acknowledged\n",
            good?"complete":"FAILED",s->direct?"host CP ring":"HWSCHED enabled");
        /* At this point START/dispatch has not run. No GPU command processors
         * own RAM, and GX must still be collapsed before CM3 reset. */
        if (!(a750_rd(gmu,0x16340)&1))
            panic("ace5-gpu: unexpected GX power before START; retain mappings");
    }
    if (good && stage>=14) {
        /* AQE requests secure context records even with preemption disabled.
         * Leave AQE off until protected allocation and the secure SID2 domain
         * are implemented. CB1 is reserved for the nonsecure LPAC SID1. */
        uint32_t start[]={0};
        good=good && a750_hfi_send(s,10,start,1,false,NULL);
        a750_log("ace5-gpu: stage 14 %s; HFI START, %u memory allocations\n",
            good?"complete":"FAILED",s->allocation_count);
        a750_read(gmu,0x16340,"GFX_PWR_CLK_AFTER_START");
        if (!(a750_rd(gmu,0x16340)&1))
            panic("ace5-gpu: unexpected GX power without OOB; retain mappings");
    }
    if (good && stage>=16) good=a750_gx_prepare(s);
    if (good && stage>=15) {
        /* Lowest stock operating point, 231 MHz / bandwidth index 1. */
        uint32_t vote[]={0,1,1,1};
        good=a750_hfi_send(s,30,vote,4,false,NULL);
        bool requested=false;
        if (good) {
            requested=true;
            a750_log("ace5-gpu: request GX through GMU OOB\n");
            a750_wr(gmu,0x16650,1u<<30);
            good=a750_poll(gmu,0x16648,1u<<31,1u<<31);
            a750_wr(gmu,0x16644,1u<<31);
            uint32_t status=a750_read(gmu,0x16340,"GFX_PWR_CLK_OOB");
            good=good && !(status&3);
            if (good) {
                vm_offset_t gx=ml_io_map(0x3d00000,0x10000);
                if (!gx) good=false;
                else {
                    a750_read(gx,0x840,"GX_RBBM_STATUS");
                    a750_read(gx,0x84c,"GX_RBBM_STATUS3");
                }
            }
        }
        a750_log("ace5-gpu: stage 15 %s; stock minimum clock vote and GX power/access\n",good?"complete":"FAILED");
        if (good && stage>=17) good=a750_gx_start(s);
        if (requested) a750_wr(gmu,0x16650,1u<<31);
        /* Stop scheduling and let RSC release the voltage/bandwidth votes. */
        uint32_t slumber[]={0,1,1};
        a750_wr(gmu,0x16100,0);
        if (!a750_hfi_send(s,33,slumber,3,false,NULL))
            panic("ace5-gpu: PREPARE_SLUMBER failed; retain mappings");
        a750_wr(gmu,0x26c40,0); a750_wr(gmu,0x163c4,1);
    }
    return good;
}
