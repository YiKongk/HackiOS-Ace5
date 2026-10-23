/* Bounds-checked ELF32 MDT metadata for the device's signed ZAP image.
 * Authentication belongs to PAS13; this parser only validates file ranges. */
#ifndef GIULIA_ZAP_FW_H
#define GIULIA_ZAP_FW_H
struct a750_zap_segment { uint32_t file, address, filesz, memsz; };
struct a750_zap_image {
    uint32_t header_size, hash_offset, hash_size, base, size, segments;
    struct a750_zap_segment segment[8];
};
static uint32_t a750_zap_u32(const uint8_t *p)
{ return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint16_t a750_zap_u16(const uint8_t *p)
{ return p[0]|(uint16_t)p[1]<<8; }
static bool a750_zap_parse(struct a750_zap_image *out, const uint8_t *fw, size_t size)
{
    struct a750_zap_image z={0};
    if (!fw || size<52 || size>1024*1024 || a750_zap_u32(fw)!=0x464c457f ||
        fw[4]!=1 || fw[5]!=1 || fw[6]!=1 || a750_zap_u16(fw+16)!=2 ||
        a750_zap_u32(fw+20)!=1 || a750_zap_u16(fw+40)!=52 ||
        a750_zap_u16(fw+42)!=32) return false;
    uint32_t phoff=a750_zap_u32(fw+28), count=a750_zap_u16(fw+44);
    if (phoff!=52 || count<2 || count>16 || (uint64_t)phoff+32*count>size) return false;
    const uint8_t *first=fw+phoff;
    if (a750_zap_u32(first)==1 || a750_zap_u32(first+4)) return false;
    z.header_size=a750_zap_u32(first+16);
    if (z.header_size<phoff+32*count || z.header_size>size) return false;
    uint64_t maximum=0;
    z.base=0xffffffff;
    for (unsigned i=1;i<count;i++) {
        const uint8_t *p=fw+phoff+32*i;
        uint32_t type=a750_zap_u32(p), file=a750_zap_u32(p+4), addr=a750_zap_u32(p+12);
        uint32_t filesz=a750_zap_u32(p+16), memsz=a750_zap_u32(p+20), flags=a750_zap_u32(p+24);
        if ((uint64_t)file+filesz>size) return false;
        if ((flags&0x7000000)==0x2000000) {
            if (z.hash_size || !filesz || file<z.header_size) return false;
            z.hash_offset=file; z.hash_size=filesz; continue;
        }
        if (type!=1 || !memsz) continue;
        if (!(flags&(1<<27)) || filesz>memsz || (addr&4095) ||
            (uint64_t)addr+memsz>0x100000000ull || file<z.header_size ||
            z.segments==8) return false;
        for (unsigned j=0;j<z.segments;j++) {
            const struct a750_zap_segment *s=&z.segment[j];
            if ((uint64_t)addr<s->address+(uint64_t)s->memsz &&
                (uint64_t)s->address<addr+(uint64_t)memsz) return false;
        }
        z.segment[z.segments++]=(struct a750_zap_segment){file,addr,filesz,memsz};
        if (addr<z.base) z.base=addr;
        uint64_t end=((uint64_t)addr+memsz+4095)&~4095ull;
        if (end>maximum) maximum=end;
    }
    if (!z.segments || !z.hash_size || (uint64_t)z.header_size+z.hash_size>16384 ||
        maximum<=z.base || maximum-z.base>8192) return false;
    for (unsigned i=0;i<z.segments;i++) {
        const struct a750_zap_segment *s=&z.segment[i];
        if ((uint64_t)s->file<z.hash_offset+(uint64_t)z.hash_size &&
            (uint64_t)z.hash_offset<s->file+(uint64_t)s->filesz) return false;
    }
    z.size=(uint32_t)(maximum-z.base); *out=z; return true;
}
#endif
