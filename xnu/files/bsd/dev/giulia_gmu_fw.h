/* Bounds-checked Gen7 GMU block format. Firmware bytes remain local to device.
 * Layout/VMAs: OnePlusOSS SM8650 adreno_gen7_gmu.c, commit 453d8875f815.
 * This header is also compiled by the host parser test without XNU headers. */
#ifndef GIULIA_GMU_FW_H
#define GIULIA_GMU_FW_H
#include <stdint.h>
#include <stdbool.h>

#define A750_FW_BLOCKS 32
enum a750_region { A750_INVALID, A750_ITCM, A750_CACHE, A750_DTCM, A750_NC, A750_NCX };
struct a750_fw_block { uint32_t address, size, type, value, payload; };
struct a750_fw { const uint8_t *bytes; uint32_t count; struct a750_fw_block blocks[A750_FW_BLOCKS]; };

static uint32_t a750_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static enum a750_region a750_region(uint32_t a, uint32_t n)
{
    uint64_t end=(uint64_t)a+n;
    if (!n || (a&3) || (n&3)) return A750_INVALID;
    if (end<=0x4000) return A750_ITCM;
    if (a>=0x4000 && end<=0x1000000) return A750_CACHE;
    if (a>=0x10004000 && end<=0x10008000) return A750_DTCM;
    if (a>=0x60000000 && end<=0x80000000) return A750_NC;
    if (a>=0xc0000000 && end<=0xe0000000) return A750_NCX;
    return A750_INVALID;
}
static bool a750_fw_parse(struct a750_fw *fw, const uint8_t *data, uint64_t size)
{
    fw->count=0; fw->bytes=data;
    if (!data || size<16 || size>1024*1024) return false;
    bool seen_data=false;
    for (uint32_t off=0;off<size;) {
        if (size-off<16 || fw->count==A750_FW_BLOCKS) return false;
        struct a750_fw_block *b=&fw->blocks[fw->count];
        b->address=a750_le32(data+off); b->size=a750_le32(data+off+4);
        b->type=a750_le32(data+off+8); b->value=a750_le32(data+off+12); b->payload=off+16;
        if (b->size>size-b->payload) return false;
        if (b->size) {
            if (!a750_region(b->address,b->size)) return false;
            seen_data=true;
            for (unsigned i=0;i<fw->count;i++) {
                const struct a750_fw_block *p=&fw->blocks[i];
                if (p->size && (uint64_t)b->address<p->address+(uint64_t)p->size &&
                    (uint64_t)p->address<b->address+(uint64_t)b->size) return false;
            }
        } else {
            if (seen_data) return false;
            if (b->type==1 || b->type==7) {
                if (!a750_region(b->address,b->value) || b->value>1024*1024) return false;
            } else if (b->type<2 || b->type>6) return false;
        }
        fw->count++; off=b->payload+b->size;
    }
    return seen_data;
}
#endif
