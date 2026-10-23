/* A750 linear BGRA display blits. Included after gpuWait() in RedwoodGPU.cpp.
 * Register layout and sequence: Mesa 25.0.7 fd6_blitter.cc / fd6_emit.cc,
 * freedreno a6xx.xml (MIT, Rob Clark / Google and Mesa contributors).
 * This is a separate, serialized command buffer; ordinary Mesa submissions may
 * run between its fences but cannot reuse its memory. No physical address is
 * accepted from userspace. The diagnostic operates on existing IOSurfaces.
 */
extern "C" uint64_t kvtophys(vm_offset_t);

struct redwood_blit {
    uint32_t source, destination; /* source zero: clear destination rectangle to transparent black */
    int32_t src[4], dst[4];
    uint32_t linear, fence;
};

static IOLock *gDisplayLock;
static IOBufferMemoryDescriptor *gDisplayCommands;
static uint64_t gDisplayCommandVA;
static uint64_t gDisplayAge;
struct display_mapping {
    MiniIOSurface *surface;
    uint8_t *pixels;
    uint64_t address, size, age;
};
static display_mapping gDisplayMaps[8], gDisplayPanel;

static uint32_t displayPacket7(unsigned op, unsigned count)
{
    return 0x70000000u | count | ((1u ^ __builtin_parity(count)) << 15) |
        (op << 16) | ((1u ^ __builtin_parity(op)) << 23);
}
static uint32_t displayPacket4(unsigned reg)
{
    return 0x40000001u | (reg << 8) | ((1u ^ __builtin_parity(reg)) << 27);
}

/* Called with both display lock and GPU lock held. Tables are owned by the
 * backend; this only imports wired pixel pages while retaining their surface. */
static bool displayMap(display_mapping *m, uint8_t *pixels, uint64_t bytes)
{
    uint64_t offset = (uintptr_t)pixels & 4095;
    uint64_t size = (bytes + offset + 4095) & ~4095ull;
    uint64_t address = redwood_gpu_address(size);
    if (!address) return false;
    bool good = true;
    for (uint64_t off = 0; off < size; off += 4096) {
        uint64_t pa = kvtophys((vm_offset_t)pixels - offset + off);
        if (!pa || !redwood_gpu_map(address + off, pa, false)) { good = false; break; }
    }
    redwood_gpu_mapped();
    if (!good) {
        redwood_gpu_unmap(address, size);
        redwood_gpu_address_free(address, size);
        return false;
    }
    m->pixels = pixels; m->address = address + offset; m->size = size;
    return true;
}
static void displayUnmap(display_mapping *m)
{
    if (m->address) {
        redwood_gpu_unmap(m->address & ~4095ull, m->size);
        redwood_gpu_address_free(m->address & ~4095ull, m->size);
    }
    if (m->surface) MiniIOSurfaceDrop(m->surface);
    bzero(m, sizeof(*m));
}
static display_mapping *displaySurface(MiniIOSurface *s, display_mapping *avoid)
{
    display_mapping *oldest = NULL;
    for (unsigned i = 0; i < 8; i++) {
        display_mapping *m = &gDisplayMaps[i];
        if (m->surface == s) { m->age = ++gDisplayAge; return m; }
        if (m != avoid && (!oldest || m->age < oldest->age)) oldest = m;
    }
    if (!oldest) return NULL;
    displayUnmap(oldest);
    if (!displayMap(oldest, s->pixels(), (uint64_t)s->info.bytesPerRow * s->info.height)) return NULL;
    oldest->surface = MiniIOSurfaceCopy(s->sid);
    if (!oldest->surface) { displayUnmap(oldest); return NULL; }
    oldest->age = ++gDisplayAge;
    return oldest;
}
static bool displaySurfaceValid(MiniIOSurface *s)
{
    return s && !s->client && s->info.pixelFormat == 0x42475241 &&
        s->info.planeCount == 0 && s->info.bytesPerElement == 4 &&
        s->info.width && s->info.height && s->info.width <= 4096 && s->info.height <= 4096 &&
        !(s->info.bytesPerRow & 63) && !((uintptr_t)s->pixels() & 63) &&
        s->info.bytesPerRow >= s->info.width * 4 &&
        (uint64_t)s->info.offset + (uint64_t)s->info.bytesPerRow * s->info.height <= s->info.allocSize;
}
static bool displayRectValid(const int32_t *r, uint32_t width, uint32_t height)
{
    return r[0] >= 0 && r[1] >= 0 && r[2] > 0 && r[3] > 0 &&
        (uint64_t)r[0] + r[2] <= width && (uint64_t)r[1] + r[3] <= height;
}

/* An isolated sysmem 2D operation. The enclosing backend submission supplies
 * context synchronization and a joined BR/BV completion fence. */
static uint32_t displayCommands(uint32_t *p, uint64_t source, uint64_t destination,
    uint32_t width, uint32_t height, uint32_t sourcePitch, uint32_t destinationPitch,
    const int32_t *src, const int32_t *dst, bool linear, bool clear)
{
    unsigned n = 0;
#define DREG(r,v) do { p[n++] = displayPacket4(r); p[n++] = (v); } while (0)
#define DOP(o,v) do { p[n++] = displayPacket7(o,1); p[n++] = (v); } while (0)
    DOP(0x65, 12); /* RM6_BLIT2DSCALE */
    DOP(0x46, 0x21); DOP(0x46, 0x20); /* clean CCU color/depth */
    DOP(0x46, 0x19); DOP(0x46, 0x18); /* invalidate only after cleaning */
    p[n++] = displayPacket7(0x26,0);
    /* A750: six CCUs, 256 KiB depth cache each; sysmem color begins at 1.5 MiB. */
    DREG(0x88e5, 0xc0000000);
    DREG(0x8401, (uint32_t)src[0] << 8);
    DREG(0x8402, (uint32_t)(src[0] + src[2] - 1) << 8);
    DREG(0x8403, (uint32_t)src[1] << 8);
    DREG(0x8404, (uint32_t)(src[1] + src[3] - 1) << 8);
    DREG(0x8405, (uint32_t)dst[0] | ((uint32_t)dst[1] << 16));
    DREG(0x8406, (uint32_t)(dst[0]+dst[2]-1) | ((uint32_t)(dst[1]+dst[3]-1) << 16));
    uint32_t control = 0x10f03000 | (clear ? 0x80 : 0);
    DREG(0x8c00, control); DREG(0x8400, control); /* RGBA8, all channels, UNORM8 */
    DREG(0xb2d2, 1u << 29); /* A7XX TPL1 source is a 2D texture */
    DREG(0xa9bf, 0xf180); /* A7XX SP_2D_DST_FORMAT */
    DREG(0x8c01, 0);
    if (clear) {
        DREG(0x8c2c, 0); DREG(0x8c2d, 0); DREG(0x8c2e, 0); DREG(0x8c2f, 0);
    }
    DREG(0xb2c0, 0x500430 | (linear ? 0x10000 : 0)); /* BGRA linear, required UNK20/22 */
    DREG(0xb2c1, width | (height << 15));
    DREG(0xb2c2, (uint32_t)source); DREG(0xb2c3, (uint32_t)(source >> 32));
    DREG(0xb2c4, sourcePitch << 3); /* A7xx pitch is bytes, unlike A6xx */
    DREG(0x8c17, 0x430); /* BGRA linear, no UBWC */
    DREG(0x8c18, (uint32_t)destination); DREG(0x8c19, (uint32_t)(destination >> 32));
    DREG(0x8c1a, destinationPitch >> 6);
    DOP(0x46, 0x3f); /* LABEL */
    p[n++] = displayPacket7(0x26,0);
    DREG(0x8e04, 1); /* A750 RB_DBG_ECO_CNTL_blit */
    DOP(0x2c, 3); /* CP_BLIT SCALE */
    p[n++] = displayPacket7(0x26,0);
    DREG(0x8e04, 0);
    DOP(0x46, 0x21); DOP(0x46, 0x31); /* clean color/UCHE before publishing pixels */
    p[n++] = displayPacket7(0x26,0);
#undef DREG
#undef DOP
    return n;
}

static bool displayBlit(MiniIOSurface *source, MiniIOSurface *target, uint8_t *panel,
    uint32_t width, uint32_t height, uint32_t stride, const int32_t *src, const int32_t *dst,
    bool linear, uint32_t *completed)
{
    if (!gDisplayLock || !gGPULock ||
        (source && (!displaySurfaceValid(source) || !displayRectValid(src, source->info.width, source->info.height))) ||
        !displayRectValid(dst, width, height) || width > 4096 || height > 4096 ||
        stride < width * 4 || (stride & 63) || (source && source == target) ||
        (target && !displaySurfaceValid(target)) || (!target && (!panel || ((uintptr_t)panel & 63)))) return false;
    bool good = false;
    uint64_t chip = 0, ns = 0;
    uint64_t began = mach_absolute_time();
    IOLockLock(gDisplayLock);
    IOLockLock(gGPULock);
    do {
        if (!redwood_gpu_ready() || !redwood_gpu_parameter(3, &chip) || (uint32_t)chip != 0x43051401) break;
        if (source && source->drawn && !gpuWait(source->drawn, 1000)) break;
        if (target && target->drawn && !gpuWait(target->drawn, 1000)) break;
        if (!gDisplayCommands) {
            gDisplayCommands = IOBufferMemoryDescriptor::withCapacity(PAGE_SIZE, kIODirectionInOut);
            display_mapping m = {};
            if (!gDisplayCommands || !displayMap(&m, (uint8_t *)gDisplayCommands->getBytesNoCopy(), PAGE_SIZE)) break;
            gDisplayCommandVA = m.address;
        }
        if (!gDisplayCommandVA) break;
        display_mapping *from = source ? displaySurface(source, NULL) : NULL, *to = NULL;
        if (source && !from) break;
        if (target) {
            to = displaySurface(target, from);
        } else {
            if (gDisplayPanel.address && gDisplayPanel.pixels != panel) break;
            if (!gDisplayPanel.address && !displayMap(&gDisplayPanel, panel, (uint64_t)stride * height)) break;
            to = &gDisplayPanel;
        }
        if (!to) break;
        /* CPU writes must have reached RAM before either resource becomes GPU-owned.
         * The firmware panel is mapped uncached; no cache operation is needed there. */
        if (source) redwood_gpu_to_memory(source->pixels(), (uint64_t)source->info.bytesPerRow * source->info.height);
        if (target) redwood_gpu_to_memory(target->pixels(), (uint64_t)stride * height);
        uint32_t *commands = (uint32_t *)gDisplayCommands->getBytesNoCopy();
        const int32_t solid[4] = {0, 0, 1, 1};
        uint32_t words = displayCommands(commands, from ? from->address : 0, to->address,
            source ? source->info.width : 1, source ? source->info.height : 1,
            source ? source->info.bytesPerRow : 64, stride, source ? src : solid, dst, linear, !source);
        redwood_gpu_to_memory(commands, words * 4);
        uint32_t fence = redwood_gpu_submit(&gDisplayCommandVA, &words, 1);
        if (!fence) break;
        if (source) source->drawn = fence;
        if (target) target->drawn = fence;
        if (!gpuWait(fence, 1000)) break;
        if (target) redwood_gpu_to_memory(target->pixels(), (uint64_t)stride * height);
        if (completed) *completed = fence;
        good = true;
    } while (false);
    IOLockUnlock(gGPULock);
    IOLockUnlock(gDisplayLock);
    absolutetime_to_nanoseconds(mach_absolute_time() - began, &ns);
    static uint32_t reported;
    if (reported++ < 8 || !good) IOLog("RedwoodGPUDisplay: %s %ux%u to %ux%u filter=%u %llu us\n",
        good ? "completed" : "refused", source ? source->info.width : 0, source ? source->info.height : 0,
        width, height, linear, ns/1000);
    return good;
}

extern "C" bool
RedwoodGPUDisplay(MiniIOSurface *source, const int32_t *src, const int32_t *dst,
    uint8_t *panel, uint32_t width, uint32_t height, uint32_t stride, bool linear)
{
    return displayBlit(source, NULL, panel, width, height, stride, src, dst, linear, NULL);
}

static IOReturn displayDiagnostic(redwood_blit *r)
{
    if (r->linear > 1 || (!r->source && r->linear) || !r->destination || r->source == r->destination) return kIOReturnBadArgument;
    MiniIOSurface *source = r->source ? MiniIOSurfaceCopy(r->source) : NULL, *target = MiniIOSurfaceCopy(r->destination);
    bool good = (!r->source || source) && target && displayBlit(source, target, NULL,
        target->info.width, target->info.height, target->info.bytesPerRow, r->src, r->dst, r->linear, &r->fence);
    if (source) MiniIOSurfaceDrop(source);
    if (target) MiniIOSurfaceDrop(target);
    return good ? kIOReturnSuccess : kIOReturnError;
}

/* GPU power is already off. In-flight DMA can no longer touch these resources. */
static void displayRelease(void)
{
    for (unsigned i = 0; i < 8; i++) displayUnmap(&gDisplayMaps[i]);
    displayUnmap(&gDisplayPanel);
    if (gDisplayCommandVA) {
        redwood_gpu_unmap(gDisplayCommandVA, PAGE_SIZE);
        redwood_gpu_address_free(gDisplayCommandVA, PAGE_SIZE);
    }
    gDisplayCommandVA = 0;
    OSSafeReleaseNULL(gDisplayCommands);
}
