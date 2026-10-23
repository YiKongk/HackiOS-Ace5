#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../xnu/files/bsd/dev/giulia_gpu_submit.h"
static unsigned parity(uint32_t v) { unsigned n=0;while(v) {n+=v&1;v>>=1;}return n&1; }
int main(void) {
    uint64_t addresses[128];uint32_t sizes[128],p[582];
    for (unsigned i=0;i<128;i++) {addresses[i]=0x80000000ull+0x10000*i;sizes[i]=i+1;}
    for (unsigned reset=0;reset<=1;reset++) for (unsigned count=1;count<=128;count++) {
        memset(p,0xa5,sizeof(p));
        unsigned n=a750_submit_words(p+1,580,addresses,sizes,count,4,5,reset);
        assert(n==42+4*count+26*reset && p[0]==0xa5a5a5a5 && p[n+1]==0xa5a5a5a5);
        unsigned ibs=0,bv=0,join=0,finish=0,at=1;
        while (at<=n) {
            uint32_t h=p[at++];unsigned words,op;
            if (h>>28==7) {
                words=h&0x3fff;op=(h>>16)&0x7f;
                assert(parity(words)+((h>>15)&1)==1 && parity(op)+((h>>23)&1)==1);
                if (op==0x3f) {assert(words==3 && p[at]==addresses[ibs] && !p[at+1] && p[at+2]==sizes[ibs]);ibs++;}
                if (op==0x46 && words==4 && p[at+1]==0x6104800c) bv=at;
                if (op==0x14 && words==4 && p[at+1]==0x6104800c) join=at;
                if (op==0x46 && words==4 && p[at+1]==0x61048004) finish=at;
            } else {
                assert(h>>28==4);words=h&0x7f;op=(h>>8)&0x3ffff;
                assert(parity(words)+((h>>7)&1)==1 && parity(op)+((h>>27)&1)==1);
            }
            assert(at+words<=n+1);at+=words;
        }
        assert(at==n+1 && ibs==count && bv && join>bv && finish>join);
        memset(p,0xa5,sizeof(p));
        assert(!a750_submit_words(p,n-1,addresses,sizes,count,4,5,reset));
        for (unsigned i=0;i<582;i++) assert(p[i]==0xa5a5a5a5);
    }
    assert(!a750_submit_words(p,580,addresses,sizes,129,4,5,true));
    assert(!a750_submit_words(p,580,addresses,sizes,1,4,0,true));
    const uint64_t bad[]={0,1,0x100000000ull,0xfffffffc};
    sizes[0]=2;
    for (unsigned i=0;i<4;i++) assert(!a750_submit_words(p,580,bad+i,sizes,1,4,5,true));
    sizes[0]=1<<20;assert(!a750_submit_words(p,580,addresses,sizes,1,4,5,true));
    puts("A7xx submission tests passed: 1..128 IBs, parity/lengths, BV-before-BR fence order, capacity and address bounds");
}
