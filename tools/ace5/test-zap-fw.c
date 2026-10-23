#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/giulia_zap_fw.h"
int main(int argc, char **argv)
{
    assert(argc==2);
    FILE *f=fopen(argv[1],"rb"); assert(f);
    assert(!fseek(f,0,SEEK_END)); long size=ftell(f); assert(size>0 && size<1024*1024);
    rewind(f); uint8_t *fw=malloc((size_t)size), *copy=malloc((size_t)size); assert(fw && copy);
    assert(fread(fw,1,(size_t)size,f)==(size_t)size); fclose(f);
    struct a750_zap_image z;
    assert(a750_zap_parse(&z,fw,(size_t)size));
    assert(z.header_size==148 && z.hash_offset==8192 && z.hash_size==3896);
    assert(z.base==4096 && z.size==4096 && z.segments==1);
    assert(z.segment[0].file==4096 && z.segment[0].filesz==1072 && z.segment[0].memsz==1072);
    for (size_t n=0;n<(size_t)size;n++) assert(!a750_zap_parse(&z,fw,n));
    for (unsigned i=0;i<10000;i++) {
        memcpy(copy,fw,(size_t)size);
        unsigned offset=(i*101u)%148; copy[offset]^=(uint8_t)(1u<<(i%8));
        if (!a750_zap_parse(&z,copy,(size_t)size)) continue;
        assert(z.size<=8192 && z.segments<=8 && z.header_size+z.hash_size<=16384);
        assert((uint64_t)z.hash_offset+z.hash_size<=(uint64_t)size);
        for (unsigned j=0;j<z.segments;j++) {
            assert((uint64_t)z.segment[j].file+z.segment[j].filesz<=(uint64_t)size);
            assert(z.segment[j].address>=z.base);
            assert((uint64_t)z.segment[j].address+z.segment[j].memsz<=z.base+(uint64_t)z.size);
        }
    }
    free(copy); free(fw);
    puts("ZAP parser passed: real image, every truncated size, 10000 header mutations");
}
