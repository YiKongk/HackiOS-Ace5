/* Actual ZAP loader with a fake PAS and reserved RAM. Supply the local signed
 * image; no firmware is committed. Build with ASan/UBSan. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
#define VM_WIMG_WCOMB 6
#define round_page(n) (((n)+16383u)&~16383u)
#define kernel_map 0
struct a750_gx { bool zap_owned; };
struct a750_hfi { struct a750_gx *gx; };
static uint8_t *fw,*metadata,region[0x4000];
static size_t bytes;
static unsigned mode,calls,mapped,unmapped,freed;
static const uint8_t *redwood_image_file(const char *dir,const char *name,uint64_t *n,int *disk) {
    assert(!strcmp(dir,"fw") && !strcmp(name,"gen70900_zap.mbn"));*disk=1;*n=bytes;return fw;
}
static vm_offset_t a750_alloc(uint32_t n) { metadata=calloc(1,round_page(n));assert(metadata);return (vm_offset_t)metadata; }
static uint64_t kvtophys(vm_offset_t p) { return 0x180000000ull+p-(vm_offset_t)metadata; }
static void a750_flush(vm_offset_t p,vm_size_t n) { assert(p==(vm_offset_t)metadata && n==0x4000); }
static void kmem_free(int map,vm_offset_t p,vm_size_t n) { assert(!map && n==0x4000);free((void *)p);freed++; }
static uint64_t mach_absolute_time(void) { return 0; }
static void absolutetime_to_nanoseconds(uint64_t t,uint64_t *n) { *n=t; }
static int a750_log(const char *fmt,...) { (void)fmt;return 0; }
static void a750_gpu_step(const char *n,uint32_t c,uint32_t a) { (void)n;(void)c;(void)a; }
static void panic(const char *msg) { (void)msg;abort(); }
static int32_t a750_scm3(uint64_t fn,uint64_t info,uint64_t id,uint64_t p1,uint64_t p2,uint64_t *answer) {
    assert(id==13);calls++;*answer=0;
    unsigned cmd=fn&255;assert(fn==(0x42000200u|cmd));
    if (cmd==1) {
        assert(info==0x82 && p1==0x180000000ull && !p2 && !mapped);
        assert(!memcmp(metadata,fw,148) && !memcmp(metadata+148,fw+8192,3896));
    } else if (cmd==2) assert(info==3 && p1==0x9b09a000 && p2==4096 && !mapped);
    else if (cmd==5) {
        assert(info==1 && !p1 && !p2 && unmapped==1);
        assert(!memcmp(region+0x2000,fw+4096,1072));
        for (unsigned i=0;i<0x4000;i++) if (i<0x2000 || i>=0x2000+1072) assert(region[i]==0xa5);
    } else assert(cmd==6 && info==1 && !p1 && !p2);
    if ((mode==1 && cmd==1) || (mode==2 && cmd==2) || (mode==3 && cmd==5)) *answer=1;
    return 0;
}
vm_offset_t ml_io_map_unmappable(vm_offset_t p,vm_size_t n,unsigned flags) {
    assert(calls==2 && p==0x9b098000 && n==0x4000 && flags==VM_WIMG_WCOMB);mapped++;return (vm_offset_t)region;
}
void ml_io_unmap(vm_offset_t p,vm_size_t n) { assert(p==(vm_offset_t)region && n==0x4000 && mapped==1);unmapped++; }
#include "../../xnu/files/bsd/dev/giulia_zap_start.h"
int main(int argc,char **argv) {
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));bytes=ftell(f);assert(bytes==12088);rewind(f);
    fw=malloc(bytes);assert(fw && fread(fw,1,bytes,f)==bytes);fclose(f);
    for (mode=0;mode<5;mode++) {
        memset(region,0xa5,sizeof(region));calls=mapped=unmapped=freed=0;
        struct a750_gx g={0};struct a750_hfi s={.gx=&g};
        if (mode==4) fw[0]^=1;
        assert(a750_zap_load(&s)==(mode==0));
        assert(g.zap_owned==(mode!=1 && mode!=4));
        assert(a750_zap_shutdown(&s) && !g.zap_owned);
        assert(calls==(mode==0 || mode==3?4:mode==1?1:mode==2?3:0));
        assert(freed==(mode==4?0:1));
        assert(mapped==unmapped && mapped==((mode==0 || mode==3)?1:0));
    }
    free(fw);puts("ZAP loader tests passed: metadata, PAS ordering, reserved bounds, unmap before auth, failures and shutdown");
}
