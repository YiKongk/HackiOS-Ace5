/* Host parser regression and bounds tests; optional argv[1] is local GMU FW.
 * cc -fsanitize=address,undefined -g tools/ace5/test-gmu-fw.c -o /tmp/test-gmu-fw
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/giulia_gmu_fw.h"

static void word(uint8_t *p, uint32_t v) {
    for (unsigned i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i));
}
int main(int argc, char **argv) {
    struct a750_fw fw;
    uint8_t valid[36]={0};
    word(valid,0x4000); word(valid+8,1); word(valid+12,4096);
    word(valid+16,0x4000); word(valid+20,4); word(valid+32,0x12345678);
    assert(a750_fw_parse(&fw,valid,sizeof(valid)) && fw.count==2);
    assert(!a750_fw_parse(&fw,NULL,36));
    for (unsigned n=0;n<sizeof(valid);n++) assert(!a750_fw_parse(&fw,valid,n));
    uint8_t bad[56];
    memcpy(bad,valid,36); word(bad+20,0xfffffffc); assert(!a750_fw_parse(&fw,bad,36));
    memcpy(bad,valid,36); word(bad+16,0xfffffffc); assert(!a750_fw_parse(&fw,bad,36));
    memcpy(bad,valid,36); word(bad+16,0x4001); assert(!a750_fw_parse(&fw,bad,36));
    memcpy(bad,valid,36); word(bad+20,3); assert(!a750_fw_parse(&fw,bad,35));
    memcpy(bad,valid,36); word(bad+8,99); assert(!a750_fw_parse(&fw,bad,36));
    memcpy(bad,valid,36); word(bad+12,0); assert(!a750_fw_parse(&fw,bad,36));
    memcpy(bad,valid,36); word(bad,0x3ffc); word(bad+12,8); assert(!a750_fw_parse(&fw,bad,36));
    memcpy(bad,valid,36); memcpy(bad+36,valid+16,20); assert(!a750_fw_parse(&fw,bad,56));
    memcpy(bad,valid,36); memcpy(bad+36,valid,16); assert(!a750_fw_parse(&fw,bad,52));
    /* Mutated lengths/addresses must never overread, regardless of acceptance. */
    uint32_t rng=1;
    for (unsigned i=0;i<10000;i++) {
        memcpy(bad,valid,36);
        rng=rng*1664525u+1013904223u; unsigned at=rng%36;
        rng=rng*1664525u+1013904223u; bad[at]^=(uint8_t)rng;
        (void)a750_fw_parse(&fw,bad,36);
    }
    if (argc>1) {
        FILE *f=fopen(argv[1],"rb"); assert(f);
        assert(!fseek(f,0,SEEK_END)); long n=ftell(f); assert(n>0 && n<=1024*1024);
        rewind(f); uint8_t *data=malloc((size_t)n); assert(data);
        assert(fread(data,1,(size_t)n,f)==(size_t)n); fclose(f);
        assert(a750_fw_parse(&fw,data,(uint64_t)n));
        printf("Local firmware accepted: %ld bytes, %u blocks\n",n,fw.count);
        free(data);
    }
    puts("GMU parser tests passed (bounds, overlap, metadata order, 10000 mutations)");
}
