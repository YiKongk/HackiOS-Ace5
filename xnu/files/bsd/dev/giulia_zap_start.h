/* Signed GPU PAS13 image, using the reserved range in PKG110's live DT.
 * Follow qcom_mdt_load and qcom_scm_pas_* ownership ordering. */
#include "giulia_zap_fw.h"
extern vm_offset_t ml_io_map_unmappable(vm_offset_t, vm_size_t, unsigned int);
extern void ml_io_unmap(vm_offset_t, vm_size_t);
static bool a750_zap_call(const char *name, unsigned command, unsigned arginfo,
    uint64_t p1, uint64_t p2)
{
    uint64_t answer=0, began=mach_absolute_time(), ns=0;
    a750_log("ace5-gpu: ZAP PAS13 %s begin\n",name);
    a750_gpu_step(name,command,arginfo);
    int32_t result=a750_scm3(0x42000200|command,arginfo,13,p1,p2,&answer);
    absolutetime_to_nanoseconds(mach_absolute_time()-began,&ns);
    a750_log("ace5-gpu: ZAP PAS13 %s status=%d answer=%#llx ms=%llu\n",name,result,answer,ns/1000000);
    /* WAITQ_SLEEP needs an interrupt handler; a timed-out interrupted call
     * may still own its arguments. Never release memory or resume userland. */
    if (result==2 || result==-1000) panic("ace5-gpu: incomplete PAS operation; retain memory");
    return result==0 && answer==0;
}
static bool a750_zap_load(struct a750_hfi *s)
{
    uint64_t bytes=0; int disk=-1;
    const uint8_t *fw=redwood_image_file("fw","gen70900_zap.mbn",&bytes,&disk);
    struct a750_zap_image z;
    if (s->gx->zap_owned || !a750_zap_parse(&z,fw,bytes)) return false;
    uint32_t meta_size=z.header_size+z.hash_size;
    vm_offset_t metadata=a750_alloc(meta_size);
    if (!metadata) return false;
    uint64_t pa=kvtophys(metadata);
    bool good=pa && !(pa&4095) && !(pa>>36);
    for (unsigned off=0;off<meta_size;off+=4096)
        if (kvtophys(metadata+off)!=pa+off) good=false;
    if (!good) goto out;
    memcpy((void *)metadata,fw,z.header_size);
    memcpy((void *)(metadata+z.header_size),fw+z.hash_offset,z.hash_size);
    a750_flush(metadata,round_page(meta_size));
    a750_log("ace5-gpu: ZAP metadata bytes=%u segments=%u load bytes=%u PA=%#llx\n",
        meta_size,z.segments,z.size,pa);
    good=a750_zap_call("INIT_IMAGE",1,0x82,pa,0);
    if (!good) goto out;
    s->gx->zap_owned=true;
    good=a750_zap_call("MEM_SETUP",2,3,0x9b09a000,z.size);
    if (!good) goto out;
    /* Map an aligned XNU 16K page, but access only the GPU's 8K carveout.
     * The surrounding PIL region is reserved in UEFI and outside XNU RAM. */
    vm_offset_t map=ml_io_map_unmappable(0x9b098000,0x4000,VM_WIMG_WCOMB);
    if (!map) { good=false; goto out; }
    volatile uint8_t *region=(volatile uint8_t *)(map+0x2000);
    a750_log("ace5-gpu: ZAP copy reserved RAM after PAS ownership setup\n");
    for (unsigned i=0;i<z.segments;i++) {
        const struct a750_zap_segment *seg=&z.segment[i];
        unsigned at=seg->address-z.base;
        for (unsigned n=0;n<seg->memsz;n++)
            region[at+n]=n<seg->filesz?fw[seg->file+n]:0;
    }
    __asm__ volatile("dsb sy":::"memory");
    for (unsigned i=0;i<z.segments;i++) {
        const struct a750_zap_segment *seg=&z.segment[i];
        unsigned at=seg->address-z.base;
        for (unsigned n=0;n<seg->memsz;n++)
            if (region[at+n]!=(n<seg->filesz?fw[seg->file+n]:0)) good=false;
    }
    ml_io_unmap(map,0x4000);
    a750_log("ace5-gpu: ZAP RAM readback=%u; CPU mapping removed before authentication\n",good);
    if (good) good=a750_zap_call("AUTH_AND_RESET",5,1,0,0);
out:
    kmem_free(kernel_map,metadata,round_page(meta_size));
    return good;
}
static bool a750_zap_shutdown(struct a750_hfi *s)
{
    if (!s->gx->zap_owned) return true;
    if (!a750_zap_call("SHUTDOWN",6,1,0,0))
        panic("ace5-gpu: PAS shutdown failed; retain GPU ownership");
    s->gx->zap_owned=false;
    return true;
}
