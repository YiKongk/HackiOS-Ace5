/* Run the actual HFI transport against a bounded fake CM3 queue on an ARM64 host.
 * cc -fsanitize=address,undefined -Wno-unused-function tools/ace5/test-gmu-hfi.c -o /tmp/test-gmu-hfi
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define NSEC_PER_USEC 1000
struct a750_memory { unsigned tables; vm_offset_t table[64]; };
static uint32_t shared[4096];
static unsigned mode;
static uint32_t a750_gpu_direct;
static int quiet_printf(const char *fmt, ...) { (void)fmt; return 0; }
#define printf quiet_printf
static uint32_t a750_rd(vm_offset_t base, uint32_t off) {
    (void)base; return off==0xcff0?0x20060001:off==0x16600?1:0;
}
static void a750_wr(vm_offset_t base, uint32_t off, uint32_t value) {
    (void)base;
    if (off!=0x16650 || value!=1 || mode==9) return;
    unsigned r=shared[16], w=shared[17], out=shared[29];
    assert(r<1024 && w<1024 && out<1024);
    uint32_t header=shared[212+r];
    unsigned id=header&255, count=id==12?4:3;
    uint32_t response[]={0x10000|(count<<8)|id,header,id==12?123:0,0};
    if (mode==1) response[id==12?3:2]=7;
    if (mode==2) response[1]^=1<<20; /* wrong sequence */
    if (mode==3) response[0]=(response[0]&~0xff00u)|(1<<8); /* too short */
    if (mode==4) response[0]=(response[0]&~0xff00u)|(255<<8); /* exceeds published data */
    for (unsigned i=0;i<4;i++) shared[212+1024+((out+i)&1023)]=response[i];
    shared[29]=(out+4)&1023; shared[16]=w;
}
static uint32_t a750_read(vm_offset_t b,uint32_t o,const char *n) { (void)n; return a750_rd(b,o); }
static bool a750_poll(vm_offset_t b,uint32_t o,uint32_t m,uint32_t v) { (void)b;(void)o;(void)m;(void)v; return true; }
static void delay_for_interval(unsigned t,unsigned u) { (void)t;(void)u; }
static void a750_flush(vm_offset_t p,vm_size_t n) { (void)p;(void)n; }
static vm_offset_t a750_find_memory(struct a750_memory *m,uint32_t v,uint32_t n) { (void)m;(void)v;(void)n; return 0; }
static bool a750_block(struct a750_memory *m,uint32_t v,uint32_t n,bool c) { (void)m;(void)v;(void)n;(void)c; abort(); }
static uint64_t kvtophys(vm_offset_t p) { return p; }
vm_offset_t ml_io_map_wcomb(vm_offset_t p,vm_size_t n) { (void)n; return p; }
static vm_offset_t ml_io_map(vm_offset_t p,vm_size_t n) { (void)n; return p; }
static void panic(const char *s) { (void)s; abort(); }
#define a750_log printf
#include "../../xnu/files/bsd/dev/giulia_gmu_hfi.h"
static bool a750_gx_prepare(struct a750_hfi *s) { (void)s; return true; }
static void a750_gx_release(struct a750_hfi *s) { (void)s; }
static bool a750_gx_start(struct a750_hfi *s) { (void)s; return true; }
static bool a750_gx_faulted(struct a750_hfi *s) { (void)s; return mode==10; }
static void a750_gpu_step(const char *op,uint32_t reg,uint32_t value) { (void)op;(void)reg;(void)value; }
static bool a750_keep_wait(struct a750_hfi *s,vm_offset_t gx,unsigned w,uint32_t f) {
    (void)s;(void)gx;(void)w;(void)f;return false;
}
#undef printf
static struct a750_hfi setup(unsigned index) {
    memset(shared,0,sizeof(shared)); mode=0;
    shared[16]=shared[17]=shared[28]=shared[29]=index;
    return (struct a750_hfi){.h=shared};
}
int main(void) {
    uint32_t packet[255]={0}, value;
    for (unsigned r=0;r<1024;r+=4) {
        struct a750_hfi s=setup(r);
        assert(a750_hfi_send(&s,15,packet,52,false,NULL));
        assert(a750_hfi_send(&s,3,packet,160,false,NULL));
        assert(a750_hfi_send(&s,12,packet,3,true,&value) && value==123);
        assert(a750_hfi_send(&s,131,packet,11,false,NULL));
    }
    for (unsigned bad=1;bad<=4;bad++) {
        struct a750_hfi s=setup(1020); mode=bad;
        assert(!a750_hfi_send(&s,3,packet,160,false,NULL));
        s=setup(1020); mode=bad;
        assert(!a750_hfi_send(&s,12,packet,3,true,&value));
    }
    struct a750_hfi s=setup(0); shared[16]=4;
    assert(!a750_hfi_write(&s,3,0,packet,4)); /* leave at least one empty word */
    assert(!a750_hfi_write(&s,3,0,packet,0));
    assert(!a750_hfi_write(&s,3,0,packet,256));
    s=setup(0); mode=10; s.gx=(struct a750_gx *)&s;
    assert(!a750_hfi_send(&s,131,packet,11,false,NULL)); /* reject ACK when DMA faults */
    assert(a750_arc_vote(0x34,1)==0x00340201);
    assert(a750_arc_vote(0x180,12)==0x0180070c);
    assert(a750_bcm_vote(0,64,16,4800000)==0x40000000);
    assert(a750_bcm_vote(100,4,4,140000)==0x60004001);
    assert(a750_bcm_vote(UINT32_MAX,64,16,4800000)==0x6fffffff);
    uint32_t alloc[11]={0,1,0,0,0x1000,2,0,1,0,4096,0};
    assert(!a750_hfi_allocate(&s,alloc,11)); /* reject secure memory */
    assert(!a750_hfi_allocate(&s,alloc,9));
    alloc[4]=0x33f; alloc[10]=31;
    assert(!a750_hfi_allocate(&s,alloc,11)); /* invalid alignment */
    puts("HFI tests passed: 256 wrap positions, ACK types/status/sequence, malformed packets, bounds, rail/BCM vectors");
}
