/* GMU firmware staging and owned 4K SMMU tables; included by giulia_adreno.c. */
#include <mach/vm_param.h>
#include <vm/vm_kern.h>
#include "giulia_gmu_fw.h"
extern uint64_t kvtophys(vm_offset_t);
extern const uint8_t *redwood_image_file(const char *, const char *, uint64_t *, int *);

#define A750_BLOCKS 64
#define A750_TABLES 2048
#define A750_PTE_ADDR 0x0000fffffffff000ull
struct a750_memblock { uint32_t va, size; vm_offset_t memory; bool cached; };
struct a750_memory {
    struct a750_fw fw;
    unsigned blocks, tables;
    struct a750_memblock block[A750_BLOCKS];
    vm_offset_t *table;
    uint64_t **leaf_cache; /* one existing L3 table per 2 MiB of the 32-bit GPU VA */
    vm_offset_t tcm_backup;
    bool tcm_saved;
};
static void a750_flush(vm_offset_t p, vm_size_t n)
{
    for (vm_offset_t q=p&~63ull;q<p+n;q+=64) __asm__ volatile("dc civac, %0"::"r"(q):"memory");
    __asm__ volatile("dsb sy":::"memory");
}
static vm_offset_t a750_alloc(uint32_t n)
{
    vm_offset_t p=0;
    if (kmem_alloc(kernel_map,&p,round_page(n),KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_IOKIT)!=KERN_SUCCESS)
        return 0;
    return p;
}
static uint64_t *a750_table(struct a750_memory *m)
{
    if (m->tables==A750_TABLES) return NULL;
    if (!m->table) {
        m->table=(vm_offset_t *)a750_alloc(A750_TABLES*sizeof(vm_offset_t));
        if (!m->table) return NULL;
    }
    vm_offset_t p=a750_alloc(4096);
    if (!p) return NULL;
    uint64_t pa=kvtophys(p);
    if (!pa || (pa&4095) || pa>>36) {
        kmem_free(kernel_map,p,round_page(4096)); return NULL;
    }
    m->table[m->tables++]=p;
    return (uint64_t *)p;
}
static uint64_t *a750_next(struct a750_memory *m, uint64_t *entry, bool create)
{
    if (!*entry && create) {
        uint64_t *p=a750_table(m);
        if (!p) return NULL;
        a750_flush((vm_offset_t)p,4096);
        *entry=kvtophys((vm_offset_t)p)|3;
        a750_flush((vm_offset_t)entry,sizeof(*entry));
    }
    if ((*entry&3)!=3) return NULL;
    for (unsigned i=0;i<m->tables;i++)
        if (kvtophys(m->table[i])==(*entry&A750_PTE_ADDR)) return (uint64_t *)m->table[i];
    return NULL;
}
static uint64_t *a750_leaf(struct a750_memory *m, uint32_t va, bool create)
{
    unsigned index=va>>21;
    if (m->leaf_cache && m->leaf_cache[index])
        return &m->leaf_cache[index][(va>>12)&511];
    if (!m->tables && (!create || !a750_table(m))) return NULL;
    uint64_t *p=(uint64_t *)m->table[0];
    p=a750_next(m,&p[va>>30],create);
    if (!p) return NULL;
    p=a750_next(m,&p[(va>>21)&511],create);
    /* Tables live until the entire address space is released. Avoid looking up
     * each table's physical address again for every 4K page of an IOSurface. */
    if (p) {
        if (!m->leaf_cache) m->leaf_cache=(uint64_t **)a750_alloc(2048*sizeof(uint64_t *));
        if (m->leaf_cache) m->leaf_cache[index]=p;
    }
    return p?&p[(va>>12)&511]:NULL;
}
static bool a750_block(struct a750_memory *m, uint32_t va, uint32_t size, bool cached)
{
    if (!size || size>1024*1024 || (va&4095) || m->blocks==A750_BLOCKS) return false;
    size=(size+4095)&~4095u;
    for (unsigned i=0;i<m->blocks;i++)
        if ((uint64_t)va<m->block[i].va+(uint64_t)m->block[i].size &&
            (uint64_t)m->block[i].va<va+(uint64_t)size) return false;
    struct a750_memblock *b=&m->block[m->blocks++];
    b->va=va; b->size=size; b->cached=cached; b->memory=a750_alloc(size);
    if (!b->memory) return false;
    for (uint32_t off=0;off<size;off+=4096) {
        uint64_t pa=kvtophys(b->memory+off);
        uint64_t *pte=a750_leaf(m,va+off,true);
        /* Observed IDR2 IAS/OAS=1, i.e. 36 physical address bits. */
        if (!pte || *pte || !pa || (pa&4095) || pa>>36) return false;
        *pte=pa|3|(2<<8)|(1<<10)|(1<<11)|(cached?4:0);
    }
    a750_log("ace5-gpu: mapped VA=%#x bytes=%#x PA(first)=%#llx cached=%u\n",va,size,kvtophys(b->memory),cached);
    return true;
}
static vm_offset_t a750_find_memory(struct a750_memory *m, uint32_t va, uint32_t n)
{
    for (unsigned i=0;i<m->blocks;i++) {
        const struct a750_memblock *b=&m->block[i];
        if (va>=b->va && (uint64_t)va+n<=b->va+(uint64_t)b->size) return b->memory+va-b->va;
    }
    return 0;
}
static bool a750_memory_prepare(struct a750_memory *m)
{
    uint64_t size=0; int disk=-1;
    const uint8_t *fw=redwood_image_file("fw","gmu_gen70900.bin",&size,&disk);
    if (!a750_fw_parse(&m->fw,fw,size)) {
        a750_log("ace5-gpu: missing or invalid local GMU firmware\n"); return false;
    }
    a750_log("ace5-gpu: GMU firmware md%d bytes=%llu blocks=%u\n",disk,size,m->fw.count);
    for (unsigned i=0;i<m->fw.count;i++) {
        const struct a750_fw_block *b=&m->fw.blocks[i];
        if (b->size || (b->type!=1 && b->type!=7)) continue;
        enum a750_region r=a750_region(b->address,b->value);
        if (r==A750_ITCM || r==A750_DTCM) continue;
        if (!a750_block(m,b->address,b->value,r==A750_CACHE)) return false;
    }
    /* Vendor GMU log: 64K. Gen7 hwsched: 17 queues, each 4K, plus headers.
     * Leave firmware-owned 0x60000000/0x60001000 allocations untouched. */
    if (!a750_block(m,0x60002000,0x10000,false) || !a750_block(m,0x60020000,0x12000,false)) return false;
    for (unsigned i=0;i<m->fw.count;i++) {
        const struct a750_fw_block *b=&m->fw.blocks[i];
        if (!b->size) continue;
        enum a750_region r=a750_region(b->address,b->size);
        if (r==A750_ITCM || r==A750_DTCM) continue;
        vm_offset_t p=a750_find_memory(m,b->address,b->size);
        if (!p) return false;
        memcpy((void *)p,fw+b->payload,b->size);
        if (memcmp((void *)p,fw+b->payload,b->size)) return false;
    }
    unsigned pages=0;
    for (unsigned i=0;i<m->blocks;i++) {
        struct a750_memblock *b=&m->block[i];
        for (uint32_t off=0;off<b->size;off+=4096) {
            uint64_t *pte=a750_leaf(m,b->va+off,false);
            if (!pte || (*pte&A750_PTE_ADDR)!=kvtophys(b->memory+off)) return false;
            pages++;
        }
        a750_flush(b->memory,b->size);
    }
    for (unsigned i=0;i<m->tables;i++) a750_flush(m->table[i],4096);
    a750_log("ace5-gpu: software page-table audit passed: %u pages, %u tables; not a hardware DMA test\n",pages,m->tables);
    m->tcm_backup=a750_alloc(0x8000);
    return m->tcm_backup!=0;
}
static bool a750_tcm_load(struct a750_memory *m, vm_offset_t gmu)
{
    if (!(a750_rd(gmu,0x16000)&1)) return false;
    uint32_t *saved=(uint32_t *)m->tcm_backup;
    for (unsigned i=0;i<0x2000;i++) saved[i]=a750_rd(gmu,0x5000+i*4);
    m->tcm_saved=true;
    for (unsigned i=0;i<0x2000;i++) a750_wr(gmu,0x5000+i*4,0);
    for (unsigned i=0;i<m->fw.count;i++) {
        const struct a750_fw_block *b=&m->fw.blocks[i];
        if (!b->size) continue;
        enum a750_region r=a750_region(b->address,b->size);
        if (r!=A750_ITCM && r!=A750_DTCM) continue;
        uint32_t off=r==A750_ITCM?0x5000+b->address:0x9000+b->address-0x10004000;
        for (unsigned j=0;j<b->size;j+=4) a750_wr(gmu,off+j,a750_le32(m->fw.bytes+b->payload+j));
        for (unsigned j=0;j<b->size;j+=4)
            if (a750_rd(gmu,off+j)!=a750_le32(m->fw.bytes+b->payload+j)) return false;
        a750_log("ace5-gpu: firmware TCM verified VA=%#x bytes=%u\n",b->address,b->size);
    }
    a750_log("ace5-gpu: firmware versions core=%#x power=%#x HFI=%#x\n",
        a750_rd(gmu,0xcfe0),a750_rd(gmu,0xcfe8),a750_rd(gmu,0xcff0));
    return true;
}
static bool a750_memory_release(struct a750_memory *m, vm_offset_t gmu)
{
    bool good=true;
    if (m->tcm_saved) {
        if (!(a750_rd(gmu,0x16000)&1)) return false;
        const uint32_t *saved=(uint32_t *)m->tcm_backup;
        for (unsigned i=0;i<0x2000;i++) a750_wr(gmu,0x5000+i*4,saved[i]);
        for (unsigned i=0;i<0x2000;i++) if (a750_rd(gmu,0x5000+i*4)!=saved[i]) good=false;
        a750_log("ace5-gpu: TCM restored=%u\n",good);
    }
    if (m->tcm_backup) kmem_free(kernel_map,m->tcm_backup,round_page(0x8000));
    for (unsigned i=0;i<m->blocks;i++) if (m->block[i].memory)
        kmem_free(kernel_map,m->block[i].memory,round_page(m->block[i].size));
    for (unsigned i=0;i<m->tables;i++) kmem_free(kernel_map,m->table[i],round_page(4096));
    if (m->table) kmem_free(kernel_map,(vm_offset_t)m->table,round_page(A750_TABLES*sizeof(vm_offset_t)));
    if (m->leaf_cache) kmem_free(kernel_map,(vm_offset_t)m->leaf_cache,round_page(2048*sizeof(uint64_t *)));
    return good;
}
