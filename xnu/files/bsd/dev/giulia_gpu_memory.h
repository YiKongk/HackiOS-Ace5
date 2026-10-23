/* Separate GPU CB0 address space (SID0, plus SID1 in host-ring mode).
 * Only explicitly imported HFI buffers
 * and this driver's firmware allocations are visible to the GPU. */
struct a750_gx {
    struct a750_memory memory;
    uint32_t smr, route, smr1, route1;
    bool owned, sid1, zap_owned;
};
static bool a750_gx_faulted(struct a750_hfi *s)
{
    uint32_t fsr=a750_rd(s->smmu,0x20058), global=a750_rd(s->smmu,0x48);
    if (!(fsr&0xc00001fe) && !global) return false;
    a750_log("ace5-gpu: GPU translation fault FSR=%#x global=%#x\n",fsr,global);
    const uint32_t offsets[]={0x50,0x54,0x58,0x1400,0x20000,0x20020,0x20024,
        0x20030,0x20060,0x20064,0x20068,0x2006c};
    for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++)
        a750_log("ace5-gpu: GPU fault SMMU +%#x=%#x\n",offsets[i],a750_rd(s->smmu,offsets[i]));
    return true;
}
static bool a750_gx_import(struct a750_gx *g, uint32_t va, vm_offset_t backing,
    uint32_t size, unsigned flags)
{
    if (!size || (va&4095) || (size&4095) || !backing) return false;
    for (unsigned off=0;off<size;off+=4096) {
        uint64_t pa=kvtophys(backing+off), *pte=a750_leaf(&g->memory,va+off,true);
        if (!pte || *pte || !pa || (pa&4095) || pa>>36) return false;
        /* Match the HFI privileged and writable flags. Initial command-buffer
         * imports are noncacheable, with explicit CPU cache synchronization. */
        *pte=pa|3|(2<<8)|(1<<10)|(1<<11)|((flags&2)?0:1<<6)|((flags&4)?0:1<<7);
    }
    a750_flush(backing,size);
    a750_log("ace5-gpu: GPU imported VA=%#x bytes=%#x flags=%#x\n",va,size,flags);
    return true;
}
static bool a750_gx_firmware(struct a750_gx *g, const char *name, uint32_t va, uint32_t expected, bool direct)
{
    uint64_t bytes=0; int disk=-1;
    const uint8_t *fw=redwood_image_file("fw",name,&bytes,&disk);
    if (!fw || bytes!=expected || bytes<8 || (bytes&3)) return false;
    /* Qualcomm adreno_get_firmware excludes the first version DWORD. */
    if (!a750_block(&g->memory,va,(uint32_t)bytes-4,!direct)) return false;
    vm_offset_t memory=a750_find_memory(&g->memory,va,(uint32_t)bytes-4);
    if (!memory) return false;
    memcpy((void *)memory,fw+4,(size_t)bytes-4);
    a750_flush(memory,(vm_size_t)bytes-4);
    for (uint32_t off=0;off<bytes-4;off+=4096)
        *a750_leaf(&g->memory,va+off,false)|=(1<<7)|(direct?1<<6:0);
    a750_log("ace5-gpu: GPU firmware %s VA=%#x bytes=%llu version=%#x\n",name,va,bytes-4,a750_le32(fw));
    return true;
}
static bool a750_gx_prepare(struct a750_hfi *s)
{
    struct a750_gx *g=(struct a750_gx *)a750_alloc(sizeof(*g));
    if (!g) return false;
    s->gx=g;
    for (unsigned i=0;i<s->allocation_count;i++) {
        const uint32_t *d=s->allocations[i];
        if (!(d[2]&1)) continue;
        if (d[1] || !a750_gx_import(g,d[0],a750_find_memory(s->memory,d[0],d[7]),d[7],d[2])) return false;
    }
    if (!a750_gx_firmware(g,"gen70900_sqe.fw",0x61000000,85364,s->direct) ||
        (!s->direct && !a750_gx_firmware(g,"gen70900_aqe.fw",0x61020000,33176,false))) return false;
    if (!a750_block(&g->memory,0x61030000,4096,false)) return false;
    if (s->direct) {
        /* Mainline SM8650: host ring, RPTR/fence page and a DMA test page.
         * GPU mappings are noncacheable; CPU ownership is transferred with
         * explicit cache maintenance. Only these owned pages are exposed. */
        const uint32_t va[]={0x61040000,0x61048000,0x61049000};
        const uint32_t size[]={0x8000,0x1000,0x1000};
        for (unsigned i=0;i<3;i++) {
            if (!a750_block(&g->memory,va[i],size[i],false)) return false;
            for (unsigned off=0;off<size[i];off+=4096)
                *a750_leaf(&g->memory,va[i]+off,false)|=1<<6;
            a750_flush(a750_find_memory(&g->memory,va[i],size[i]),size[i]);
        }
    }
    a750_flush(a750_find_memory(&g->memory,0x61030000,4096),4096);
    for (unsigned i=0;i<g->memory.tables;i++) a750_flush(g->memory.table[i],4096);
    vm_offset_t smmu=s->smmu;
    a750_log("ace5-gpu: inspect GPU SID0/context bank 0\n");
    g->smr=a750_read(smmu,0x800,"GPU_SMR0");
    g->route=a750_read(smmu,0xc00,"GPU_S2CR0");
    if ((g->smr&0x80000000) || (a750_read(smmu,0x20000,"GPU_CB0_SCTLR")&1)) return false;
    if (a750_read(smmu,0x20058,"GPU_CB0_FSR")&0xc00001fe) return false;
    if (s->direct) {
        g->smr1=a750_read(smmu,0x804,"GPU_SMR1");
        g->route1=a750_read(smmu,0xc04,"GPU_S2CR1");
        if (g->smr1&0x80000000) return false;
        g->sid1=true;
    }
    g->owned=true;
    const uint32_t offsets[]={0x1800,0x1000,0x20010,0x20030,0x20020,0x20024,
        0x20028,0x2002c,0x20038,0x2003c,0x20004};
    uint64_t root=kvtophys(g->memory.table[0]);
    const uint32_t values[]={1,0x1f300,0x38011,0x803519,(uint32_t)root,(uint32_t)(root>>32),
        0,0,0xf404ff44,0,0x32b};
    const uint32_t masks[]={1,0x3f3ff,0x3801f,0x803fff,0xfffff000,0xffff000f,
        0xfffff000,0xffff000f,0xffffffff,0xffffffff,0xffffffff};
    a750_wr(smmu,0x20000,0);
    for (unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        a750_wr(smmu,offsets[i],values[i]);
        uint32_t read=a750_rd(smmu,offsets[i]);
        a750_log("ace5-gpu: GPU SMMU +%#x wrote=%#x read=%#x\n",offsets[i],values[i],read);
        if ((read&masks[i])!=(values[i]&masks[i])) return false;
    }
    a750_wr(smmu,0x20610,0); a750_wr(smmu,0x207f0,0xffffffff);
    if (!a750_poll(smmu,0x207f4,1,0)) return false;
    a750_wr(smmu,0x20000,0x27); a750_wr(smmu,0xc00,0); a750_wr(smmu,0x800,0x80000000);
    if (g->sid1) {
        /* Linux sm8650.dtsi attaches SID0 and SID1 to the same GPU domain. */
        a750_wr(smmu,0xc04,0); a750_wr(smmu,0x804,0x80000001);
    }
    bool good=(a750_rd(smmu,0x20000)&0x1f7)==0x27 && a750_rd(smmu,0x800)==0x80000000 &&
        (a750_rd(smmu,0xc00)&0x300ff)==0;
    if (g->sid1) good=good && a750_rd(smmu,0x804)==0x80000001 && !(a750_rd(smmu,0xc04)&0x300ff);
    a750_log("ace5-gpu: stage 16 %s; %s, separate GPU tables and firmware backing\n",
        good?"complete":"FAILED",s->direct?"SID0+1/CB0, host ring":"SID0/CB0, HWSCHED");
    return good;
}
static void a750_gx_release(struct a750_hfi *s)
{
    struct a750_gx *g=s->gx;
    if (!g) return;
    vm_offset_t smmu=s->smmu;
    if (!(a750_rd(s->gmu,0x16340)&1)) panic("ace5-gpu: GPU still powered at mapping teardown");
    if (g->owned) {
        a750_read(smmu,0x20058,"GPU_CB0_FSR_AFTER_RUN");
        a750_wr(smmu,0xc00,0x20000); a750_wr(smmu,0x800,g->smr);
        if (g->sid1) { a750_wr(smmu,0xc04,0x20000); a750_wr(smmu,0x804,g->smr1); }
        a750_wr(smmu,0x20000,0);
        a750_wr(smmu,0x20058,a750_rd(smmu,0x20058)&0xc00001fe);
        a750_wr(smmu,0x20610,0); a750_wr(smmu,0x207f0,0xffffffff);
        if (!a750_poll(smmu,0x207f4,1,0)) panic("ace5-gpu: GPU TLB drain failed");
        a750_wr(smmu,0x20020,0); a750_wr(smmu,0x20024,0);
        a750_wr(smmu,0x20028,0); a750_wr(smmu,0x2002c,0); a750_wr(smmu,0xc00,g->route);
        if (g->sid1) a750_wr(smmu,0xc04,g->route1);
    }
    a750_memory_release(&g->memory,s->gmu);
    kmem_free(kernel_map,(vm_offset_t)g,round_page(sizeof(*g)));
    s->gx=NULL;
    a750_log("ace5-gpu: GPU stream removed, context disabled, owned mappings released\n");
}
