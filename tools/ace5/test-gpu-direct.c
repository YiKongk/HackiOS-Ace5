/* Exercise the actual bounded direct-ring code, including negative DMA cases.
 * cc -fsanitize=address,undefined -Wno-unused-function tools/ace5/test-gpu-direct.c -o /tmp/test-gpu-direct
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define NSEC_PER_USEC 1000
#define NSEC_PER_MSEC 1000000
struct a750_memory { unsigned unused; };
struct a750_gx { struct a750_memory memory; };
struct a750_hfi { vm_offset_t gmu,smmu; struct a750_gx *gx; bool direct; unsigned stage; };
static uint32_t regs[0x40000/4],ring[0x8000/4],memptrs[1024],scratch[1024];
static unsigned mode,submitted,waits;
static bool zap_success;
static uint32_t a750_gpu_keep;
static int a750_log(const char *fmt,...) { (void)fmt; return 0; }
static uint32_t a750_rd(vm_offset_t b,uint32_t o) { return b==1?regs[o/4]:0; }
static void a750_wr(vm_offset_t b,uint32_t o,uint32_t v) {
    if (b!=1) return;
    regs[o/4]=(mode==5 && o==0x802*4)?v^1:v;
    if (o!=0x807*4) return;
    submitted++;
    assert(regs[0x808]==1);
    assert(regs[0x800]==0x61040000 && regs[0x804]==0x61048000);
    if (v==15) {
        const uint32_t expected[]={0x70970001,0x08000000,0x70c80007,0x94b,3,
            0x20000000,2,0x61030000,0,0x80000000,0x70460004,0x08000031,0x61048004,0,1};
        assert(!memcmp(ring,expected,sizeof(expected)));
    } else if (v==24) {
        const uint32_t expected[]={0x703d8003,0x61049000,0,0xa7500001,
            0x70460004,0x08000031,0x61048004,0,2};
        assert(v==24 && !memcmp(ring+15,expected,sizeof(expected)));
    } else if (v==31) {
        const uint32_t expected[]={0x70e60001,0,0x70460004,0x08000031,0x61048004,0,3};
        assert(zap_success && !memcmp(ring+24,expected,sizeof(expected)));
    } else if (v==40) {
        const uint32_t expected[]={0x703d8003,0x61049004,0,0xa7500019,
            0x70460004,0x08000031,0x61048004,0,4};
        assert(v==40 && zap_success && !memcmp(ring+31,expected,sizeof(expected)));
    } else {
        assert(v==112 && zap_success && ring[40]==0x70970001 && ring[41]==0x80000001);
        assert(scratch[64]==0x70970001 && scratch[65]==1 && scratch[66]==0x703d8003);
        scratch[2]=0xa75000b1;
        if (mode!=11) scratch[3]=0xa75000b2;
        memptrs[3]=5;
    }
    /* Only this fake GPU may write completion/scratch data. Mode 1 supplies
     * RPTR without a fence; mode 2 supplies fences without the data write. */
    regs[0x806]=v; memptrs[0]=v;
    if (mode!=1) memptrs[1]=submitted;
    if (v==24 && mode!=2) scratch[0]=0xa7500001;
    if (v==40 && mode!=8) scratch[1]=0xa7500019;
}
static uint32_t a750_read(vm_offset_t b,uint32_t o,const char *n) { (void)n;return a750_rd(b,o); }
static bool a750_gx_faulted(struct a750_hfi *s) { (void)s;return mode==3; }
static void a750_flush(vm_offset_t p,vm_size_t n) { (void)p;(void)n; }
static void delay_for_interval(unsigned t,unsigned n) { (void)t;(void)n;assert(++waits<=4000); }
static vm_offset_t a750_find_memory(struct a750_memory *m,uint32_t va,uint32_t n) {
    (void)m;(void)n;
    return va==0x61040000?(vm_offset_t)ring:va==0x61048000?(vm_offset_t)memptrs:
        va==0x61049000?(vm_offset_t)scratch:0;
}
static void PE_parse_boot_argn(const char *n,void *v,unsigned size) { (void)n;(void)v;(void)size; }
static vm_offset_t ml_io_map(vm_offset_t p,vm_size_t n) { (void)p;(void)n;return 1; }
static void a750_bits(vm_offset_t b,uint32_t o,uint32_t m,uint32_t v) { (void)b;(void)o;(void)m;(void)v; }
static bool a750_poll(vm_offset_t b,uint32_t o,uint32_t m,uint32_t v) { (void)b;(void)o;(void)m;(void)v;return true; }
static bool a750_hfi_send(struct a750_hfi *s,unsigned id,uint32_t *p,unsigned n,bool value,uint32_t *out) {
    (void)s;(void)id;(void)p;(void)n;(void)value;(void)out;return false;
}
static bool a750_zap_load(struct a750_hfi *s) { (void)s;return zap_success; }
static bool a750_zap_shutdown(struct a750_hfi *s) { (void)s;return true; }
static bool a750_keep_wait(struct a750_hfi *s,vm_offset_t gx,unsigned w,uint32_t f) {
    (void)s;(void)gx;(void)w;(void)f;return false;
}
#include "../../xnu/files/bsd/dev/giulia_gpu_start.h"
int main(void) {
    for (mode=0;mode<=11;mode++) {
        memset(regs,0,sizeof(regs));memset(ring,0,sizeof(ring));
        memset(memptrs,0,sizeof(memptrs));memset(scratch,0,sizeof(scratch));
        submitted=waits=0;
        struct a750_gx g={0};struct a750_hfi s={.gmu=2,.smmu=3,.gx=&g,.direct=true};
        s.stage=mode>=10?20:mode>=7?19:17;zap_success=mode>=8;
        if (mode==4) regs[GEN7_CP_HW_FAULT]=1;
        if (mode==6) regs[0x806]=1;
        assert(a750_direct_start(&s,1)==(mode==0 || mode==9 || mode==10));
        if (mode==5 || mode==6) assert(submitted==0);
        else assert(regs[0x808]==3);
        if (mode==0) assert(submitted==2 && scratch[0]==0xa7500001 && memptrs[1]==2);
        if (mode==7) assert(submitted==2); /* auth failure forbids mode switch */
        if (mode==9) assert(submitted==4 && scratch[1]==0xa7500019 && memptrs[1]==4);
        if (mode==10) assert(submitted==5 && scratch[3]==0xa75000b2 && memptrs[3]==5);
    }
    puts("Direct CP tests passed: packet vectors, DMA/fences, missing writes, faults, readback, stale ring, bounded wait");
}
