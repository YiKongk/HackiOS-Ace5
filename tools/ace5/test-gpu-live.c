/* Host tests of the real A750 backend: address permissions, TLB commit,
 * all ring wrap positions, full-ring failure and fence comparisons. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define NSEC_PER_USEC 1000
struct a750_memory { uint64_t entries[64]; };
struct a750_gx { struct a750_memory memory; };
struct a750_hfi { struct a750_gx *gx; vm_offset_t smmu,gmu; };
static uint32_t regs[0x4000],ring[8192],mem[16];
static unsigned waits,tlbs,mode;
static bool a750_runtime_active;
uint32_t gpu_log,gpu_va;
static uint64_t mach_absolute_time(void) { return 0; }
static uint64_t *a750_leaf(struct a750_memory *m,uint32_t va,bool create) {
    (void)create;return va>=0x70000000 && va<0x70040000?&m->entries[(va-0x70000000)/4096]:NULL;
}
static void a750_flush(vm_offset_t p,vm_size_t n) { (void)p;(void)n; }
static uint32_t a750_rd(vm_offset_t b,uint32_t o) { return b==1?regs[o/4]:0; }
static void a750_wr(vm_offset_t b,uint32_t o,uint32_t v) {
    if (b==1) regs[o/4]=v;
    else if (o==0x207f0) tlbs++;
}
static bool a750_poll(vm_offset_t b,uint32_t o,uint32_t m,uint32_t v) { (void)b;(void)o;(void)m;(void)v;return true; }
static bool a750_gx_faulted(struct a750_hfi *s) { (void)s;return mode==1; }
static bool a750_gpu_idle(struct a750_hfi *s,vm_offset_t gx) { (void)s;(void)gx;return true; }
static int a750_log(const char *fmt,...) { (void)fmt;return 0; }
static void delay_for_interval(unsigned t,unsigned u) { (void)t;(void)u;assert(++waits<=5000); }
static void panic(const char *s) { (void)s;abort(); }
#include "../../xnu/files/bsd/sys/redwood_gpu_backend.h"
#include "../../xnu/files/bsd/dev/giulia_gpu_submit.h"
#include "../../xnu/files/bsd/dev/giulia_gpu_live.h"
static void a750_live_stop(void) {}
int main(void) {
    struct a750_gx g={0};struct a750_hfi s={.gx=&g,.smmu=2,.gmu=3};
    a750_live.session=&s;a750_live.gx=1;a750_live.ring=ring;a750_live.memptrs=mem;
    assert(a750_live_map(0x70000000,0x180000000,false));
    assert((g.memory.entries[0]&0xfc)==0x40);
    assert(!a750_live_map(0x70000000,0x180000000,false));
    assert(a750_live_map(0x70001000,0x180004000,true));
    assert((g.memory.entries[1]&0xfc)==0xc0);
    assert(!a750_live_map(0x61000000,0x180000000,false));
    assert(!a750_live_map(0xff000000,0x180000000,false));
    assert(!a750_live_map(0x70002000,0x1000000000ull,false));
    a750_live_mapped();assert(tlbs==1);
    a750_live_unmap(0x70000000,8192);assert(tlbs==2 && !g.memory.entries[0] && !g.memory.entries[1]);
    uint64_t address=0x70000000;uint32_t size=14,expected[580];
    unsigned n=a750_submit_words(expected,580,&address,&size,1,5,6,true);assert(n==72);
    for (unsigned at=0;at<8192;at++) {
        memset(ring,0xa5,sizeof(ring));a750_live.wptr=regs[0x806]=at;a750_live.fence=5;
        assert(a750_live_submit(&address,&size,1)==6);
        assert(regs[0x807]==((at+n)&8191));
        for (unsigned i=0;i<n;i++) assert(ring[(at+i)&8191]==expected[i]);
        assert(ring[(at+n)&8191]==0xa5a5a5a5);
    }
    assert(!a750_live_submit(NULL,&size,1));
    assert(!a750_live_submit(&address,&size,129));
    mem[1]=6;assert(a750_live_done(6) && !a750_live_done(7));
    mem[1]=1;assert(a750_live_done(0xffffffff));
    a750_live.wptr=0;regs[0x806]=1;waits=0;
    assert(!a750_live_submit(&address,&size,1) && waits==5000 && a750_live.failed);
    a750_live.failed=false;regs[0x806]=0;mode=1;
    assert(!a750_live_submit(&address,&size,1) && a750_live.failed);
    uint64_t value;assert(a750_live_parameter(3,&value) && value==0x443051401ull);
    assert(a750_live_parameter(2,&value) && value==3*1024*1024);
    puts("Live A750 tests passed: mapping permissions/bounds, TLB commits, 8192 ring positions, full ring, faults and fence wrap");
}
