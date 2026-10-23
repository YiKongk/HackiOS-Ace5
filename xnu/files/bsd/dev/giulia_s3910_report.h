/* Decoder for the volatile report layout configured by giulia_s3910_hw.h. */
#ifndef GIULIA_S3910_REPORT_H
#define GIULIA_S3910_REPORT_H
struct s39_contact { uint32_t x,y; };
static int s39_decode(const uint8_t *p, unsigned n, unsigned max_x, unsigned max_y,
    struct s39_contact contact[10], unsigned *mask) {
    if (!n || p[0]>10 || n!=1u+5u*p[0] || !max_x || !max_y) return -1;
    unsigned seen=0, active=0;
    for (unsigned i=0;i<p[0];i++) {
        const uint8_t *q=p+1+5*i;
        unsigned id=q[0]&15, kind=q[0]>>4, x=q[1]|q[2]<<8, y=q[3]|q[4]<<8;
        if (id>=10 || (seen&(1u<<id)) || x>max_x || y>max_y) return -1;
        seen |= 1u<<id;
        if (kind!=1 && kind!=2) continue; /* finger or glove; omit palms/hover */
        contact[id].x=(uint32_t)x*65535/max_x;
        contact[id].y=(uint32_t)y*65535/max_y;
        active |= 1u<<id;
    }
    *mask=active;
    return 0;
}
#endif
