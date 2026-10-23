/* Ace 5 / giulia S3910 transport. Only the verified PKG110 wiring is supported.
 * Register/protocol facts: OnePlusOSS sm8650_b_16.0.0_ace_5, pinctrl-pineapple.c,
 * rpmh-rsc.c and synaptics_hbp; live giulia DT. No firmware/flash commands.
 * The includer provides s39_rd, s39_wr and s39_delay (microseconds).
 */
#ifndef GIULIA_S3910_HW_H
#define GIULIA_S3910_HW_H
#define S39_TLMM_PA 0x0f100000ULL /* TLMM + GPIO register bank offset */
#define S39_RSC_PA  0x17a20000ULL
#define S39_DB_PA   0x81c60000ULL
#define S39_MAX 512
struct s39_hw {
    uintptr_t gpio, rsc, db;
    uint32_t regulator, rsc_id, last_status, slot;
    uint8_t header[4], data[S39_MAX + 3];
    unsigned length, max_x, max_y, setup_step;
};
static uintptr_t s39_cfg(struct s39_hw *h, unsigned pin) { return h->gpio + 0x1000 * pin; }
static void s39_set(struct s39_hw *h, unsigned pin, unsigned high) {
    s39_wr(s39_cfg(h, pin) + 4, high ? 2 : 0);
}
static unsigned s39_get(struct s39_hw *h, unsigned pin) {
    return s39_rd(s39_cfg(h, pin) + 4) & 1;
}
static void s39_pin(struct s39_hw *h, unsigned pin, unsigned output, unsigned high) {
    if (output) s39_set(h, pin, high);
    /* Preserve eGPIO/security bits; mux GPIO, no pull, 6 mA. */
    uint32_t v = s39_rd(s39_cfg(h, pin));
    s39_wr(s39_cfg(h, pin), (v & ~0x3ffu) | (2 << 6) | (output << 9));
}
static uint16_t s39_le16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t s39_le32(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint32_t s39_find_ldob4(struct s39_hw *h) {
    const uint8_t *db = (const uint8_t *)h->db;
    if (s39_le32(db + 4) != 0x0c0330db) return 0;
    /* cmd-db: eight 16-byte slave descriptors followed by checksum/reserved. */
    for (unsigned s = 0; s < 8; s++) {
        const uint8_t *d = db + 8 + s * 16;
        unsigned off = s39_le16(d + 2), n = s39_le16(d + 6);
        if (!s39_le16(d) || n > 512 || off + n * 24 > 0x20000 - 144) continue;
        for (unsigned i = 0; i < n; i++) {
            const uint8_t *e = db + 144 + off + 24 * i;
            if (e[0]=='l' && e[1]=='d' && e[2]=='o' && e[3]=='b' && e[4]=='4' && !e[5] && !e[6] && !e[7])
                return s39_le32(e + 16);
        }
    }
    return 0;
}
static int s39_power(struct s39_hw *h) {
    h->regulator = s39_find_ldob4(h);
    h->rsc_id = s39_rd(h->rsc);
    unsigned major = (h->rsc_id >> 16) & 255;
    if (!h->regulator || ((h->regulator >> 16) & 15) != 4) return -10;
    if (major != 3) return -11; /* Only the observed SM8650 v3 layout. */
    /* DT: ACTIVE TCS are 0..2. Use only an idle, unarmed slot. IRQ registers
     * are global; per-slot v3 stride is 672 bytes (non hardware-channel DRV2). */
    uintptr_t base = h->rsc + 0xd00, t = 0;
    for (unsigned slot=0; slot<3; slot++) {
        uintptr_t candidate=base+slot*672;
        if (s39_rd(candidate+0x28) && !(s39_rd(candidate+0x24)&0x1000000) && !s39_rd(candidate+0x2c)) {
            t=candidate; h->slot=slot; break;
        }
    }
    if (!t) return -12;
    uint32_t mask=1u<<h->slot, irq = s39_rd(base), values[3] = {1800, 1, 7};
    uint32_t control=s39_rd(t+0x24), wait=s39_rd(t+0x20);
    s39_wr(t+0x24, control & ~0x10000u);
    uint32_t saved[9];
    for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 3; j++)
        saved[i*3+j] = s39_rd(t + 0x34 + i*24 + j*4);
    s39_wr(base + 8, mask);
    for (unsigned i = 0; i < 3; i++) {
        s39_wr(t + 0x34 + i*24, 0x10108);
        s39_wr(t + 0x38 + i*24, h->regulator + i*4);
        s39_wr(t + 0x3c + i*24, values[i]);
    }
    s39_wr(t + 0x20, 7); s39_wr(t + 0x2c, 7); s39_wr(base, irq | mask);
    s39_wr(t + 0x24, 0x10000); s39_wr(t + 0x24, 0x1010000);
    unsigned i;
    for (i=0; i<2000 && !(s39_rd(base+4)&mask); i++) s39_delay(100);
    h->last_status = s39_rd(base+4);
    /* Do not overwrite in-flight slots on timeout. Reboot resets the controller. */
    if (i == 2000) return -13;
    s39_wr(t+0x24, 0x10000); s39_wr(t+0x24, 0);
    s39_wr(t+0x2c, 0); s39_wr(t+0x20, wait); s39_wr(base+8, mask); s39_wr(base, irq);
    for (unsigned a=0; a<3; a++) for (unsigned j=0; j<3; j++)
        s39_wr(t+0x34+a*24+j*4, saved[a*3+j]);
    s39_wr(t+0x24, control);
    s39_pin(h, 108, 1, 1); /* AVDD enable, DT power-on-state=1 */
    s39_delay(200000);
    return 0;
}
static void s39_bus_init(struct s39_hw *h) {
    s39_pin(h, 51, 1, 1); /* CS inactive first */
    s39_pin(h, 50, 1, 0); s39_pin(h, 49, 1, 1); s39_pin(h, 48, 0, 0);
    s39_pin(h, 162, 0, 0);
}
static void s39_reset(struct s39_hw *h) {
    s39_pin(h, 161, 1, 0); s39_delay(10000);
    s39_set(h, 161, 1); s39_delay(80000);
}
/* Mode 0, MSB first. Conservative bit-bang transport avoids GENI firmware/clock
 * assumptions. Reading clocks out 0xff, as the original Linux transport does. */
static void s39_spi(struct s39_hw *h, const uint8_t *tx, uint8_t *rx, unsigned n) {
    s39_set(h, 51, 0); s39_delay(2);
    for (unsigned i=0; i<n; i++) {
        uint8_t v = tx ? tx[i] : 0xff, r = 0;
        for (unsigned b=0; b<8; b++) {
            s39_set(h, 49, (v >> (7-b)) & 1); s39_delay(1);
            s39_set(h, 50, 1); s39_delay(1);
            r = (r << 1) | s39_get(h, 48);
            s39_set(h, 50, 0);
        }
        if (rx) rx[i] = r;
    }
    s39_delay(2); s39_set(h, 51, 1); s39_delay(10);
}
/* Return report/status code, or a negative transport error. */
static int s39_read(struct s39_hw *h) {
    h->length = 0;
    s39_spi(h, 0, h->header, 4);
    if (h->header[0] != 0xa5) return -20;
    unsigned n = s39_le16(h->header+2);
    if (h->header[1] == 0 || h->header[1] == 0xff) return 0;
    if (n > S39_MAX) return -21;
    if (n) {
        s39_spi(h, 0, h->data, n+3);
        if (h->data[0]!=0xa5 || h->data[1]!=3 || h->data[n+2]!=0x5a) return -22;
        for (unsigned i=0; i<n; i++) h->data[i] = h->data[i+2];
    }
    h->length = n;
    return h->header[1];
}
static int s39_command(struct s39_hw *h, uint8_t cmd, const uint8_t *p, unsigned n) {
    uint8_t out[259];
    if (n > 256) return -23;
    out[0]=cmd; out[1]=n; out[2]=n>>8;
    for (unsigned i=0; i<n; i++) out[i+3]=p[i];
    s39_spi(h, out, 0, n+3);
    for (unsigned i=0; i<100; i++) {
        s39_delay(2000);
        if (s39_get(h, 162)) continue;
        int r = s39_read(h);
        if (r < 0 || (r > 0 && r < 0x10)) return r;
    }
    return -24;
}
/* Configure a compact volatile report: count, then {index:4,class:4,x:16,y:16}.
 * No configuration commit, firmware update or nonvolatile write is issued. */
static const uint8_t s39_report_config[] = {0x18,8, 1, 6,4, 7,4, 8,16, 9,16, 3,0};
static int s39_start_reports(struct s39_hw *h) {
    uint8_t p[256]={0}; int r;
    h->setup_step=1;
    if ((r=s39_command(h,0x20,0,0)) != 1) return r;
    if (h->length<40 || s39_le16(h->data+2)) return -30;
    h->max_x=s39_le16(h->data+32); h->max_y=s39_le16(h->data+34);
    unsigned size=s39_le16(h->data+12);
    if (!h->max_x || !h->max_y || size<sizeof(s39_report_config) || size>sizeof(p)) return -31;
    h->setup_step=2; p[0]=0x23;
    if ((r=s39_command(h,6,p,1)) != 1) return r; /* stop HBP frames */
    h->setup_step=3; p[0]=0x11; p[1]=2; p[2]=0;
    if ((r=s39_command(h,0x24,p,3)) != 1) return r; /* LBP */
    h->setup_step=4; p[0]=9; p[1]=0;
    if ((r=s39_command(h,0x24,p,3)) != 1) return r; /* normal sensing */
    h->setup_step=5;
    for (unsigned i=0;i<size;i++) p[i]=i<sizeof(s39_report_config)?s39_report_config[i]:0;
    if ((r=s39_command(h,0x26,p,size)) != 1) return r;
    h->setup_step=6;
    if ((r=s39_command(h,0x25,0,0)) != 1) return r;
    if (h->length<sizeof(s39_report_config)) return -32;
    for (unsigned i=0;i<sizeof(s39_report_config);i++) if (h->data[i]!=s39_report_config[i]) return -32;
    h->setup_step=7; p[0]=0x11;
    if ((r=s39_command(h,5,p,1)) != 1) return r;
    h->setup_step=8;
    return 0;
}
#endif
