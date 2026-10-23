#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "../../xnu/files/bsd/dev/giulia_s3910_report.h"
int main(int argc, char **argv) {
    struct s39_contact c[10]={{0}}; unsigned mask=123;
    /* Two contacts at opposite display corners: finger 0 and gloved finger 9. */
    uint8_t p[]={2,0x10,0,0,0,0,0x29,0x5f,0x31,0x97,0x6c};
    assert(!s39_decode(p,sizeof(p),12639,27799,c,&mask));
    assert(mask==513 && c[0].x==0 && c[9].x==65535 && c[9].y==65535);
    uint8_t up[]={0}; assert(!s39_decode(up,1,12639,27799,c,&mask) && mask==0);
    assert(s39_decode(p,sizeof(p)-1,12639,27799,c,&mask)<0);
    p[6]=0x10; assert(s39_decode(p,sizeof(p),12639,27799,c,&mask)<0); /* duplicate id */
    p[6]=0x1a; assert(s39_decode(p,sizeof(p),12639,27799,c,&mask)<0); /* id out of range */
    p[6]=0x69; assert(!s39_decode(p,sizeof(p),12639,27799,c,&mask) && mask==1); /* palm */
    p[6]=0x19; p[7]=0xff; assert(s39_decode(p,sizeof(p),12639,27799,c,&mask)<0);
    assert(s39_decode(p,0,12639,27799,c,&mask)<0);
    assert(s39_decode(up,1,0,27799,c,&mask)<0);
    if (argc==2) {
        FILE *f=fopen(argv[1],"rb"); assert(f); int n; unsigned total=0;
        while ((n=fgetc(f))!=EOF) {
            uint8_t data[255]; assert(n>0 && fread(data,1,n,f)==(unsigned)n);
            assert(!s39_decode(data,n,12639,27799,c,&mask)); total++;
        }
        fclose(f); printf("Decoded %u captured device reports\n",total);
    }
    puts("S3910 report bounds, scaling, multi-touch and release checks passed");
}
