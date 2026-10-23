/* Hardware display scaler readback. Runs as a /redwood/bin diagnostic.
 * The CPU reference uses pixel-center sampling and also checks untouched pixels
 * around cropped destinations. No CPU renderer writes the expected destination.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOSurface/IOSurfaceRef.h>
#include <mach/mach_time.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct blit { uint32_t source, destination; int32_t src[4], dst[4]; uint32_t linear, fence; };
static void number(CFMutableDictionaryRef d, CFStringRef k, int64_t n)
{
    CFNumberRef value=CFNumberCreate(NULL,kCFNumberSInt64Type,&n);
    CFDictionarySetValue(d,k,value); CFRelease(value);
}
static IOSurfaceRef surface(unsigned w,unsigned h)
{
    CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    number(d,kIOSurfaceWidth,w);number(d,kIOSurfaceHeight,h);number(d,kIOSurfaceBytesPerElement,4);
    number(d,kIOSurfaceBytesPerRow,(w*4+63)&~63u);number(d,kIOSurfacePixelFormat,0x42475241);
    IOSurfaceRef s=IOSurfaceCreate(d);CFRelease(d);return s;
}
static uint32_t pixel(unsigned x,unsigned y,unsigned w,unsigned h)
{
    return 0xff000000u | (x*191/(w-1)) | ((y*127/(h-1))<<8) | (((x^y)&31)<<16);
}
static int clampi(int x,int lo,int hi) { return x<lo?lo:x>hi?hi:x; }
static uint32_t reference(unsigned x,unsigned y,unsigned w,unsigned h,const struct blit *b)
{
    if (x<(unsigned)b->dst[0] || y<(unsigned)b->dst[1] || x>=(unsigned)(b->dst[0]+b->dst[2]) || y>=(unsigned)(b->dst[1]+b->dst[3])) return 0xa5a5a5a5;
    if (!b->source) return 0;
    double sx=b->src[0]+((x-b->dst[0]+.5)*b->src[2])/b->dst[2]-.5;
    double sy=b->src[1]+((y-b->dst[1]+.5)*b->src[3])/b->dst[3]-.5;
    /* The 2D engine samples in 8 fractional bits. At the native panel ratio,
     * e.g. destination x=777, 691.499604 rounds to 691.5 before nearest
     * selection. Keep an exact pixel comparison against that quantization. */
    if (!b->linear) return pixel(clampi((int)floor(round(sx*256)/256+.5),0,w-1),
                                clampi((int)floor(round(sy*256)/256+.5),0,h-1),w,h);
    int x0=(int)floor(sx),y0=(int)floor(sy);double fx=sx-x0,fy=sy-y0;
    uint32_t c[4]={pixel(clampi(x0,0,w-1),clampi(y0,0,h-1),w,h),pixel(clampi(x0+1,0,w-1),clampi(y0,0,h-1),w,h),
        pixel(clampi(x0,0,w-1),clampi(y0+1,0,h-1),w,h),pixel(clampi(x0+1,0,w-1),clampi(y0+1,0,h-1),w,h)};
    uint32_t result=0;
    for(unsigned k=0;k<4;k++) {
        double top=((c[0]>>(k*8))&255)*(1-fx)+((c[1]>>(k*8))&255)*fx;
        double bottom=((c[2]>>(k*8))&255)*(1-fx)+((c[3]>>(k*8))&255)*fx;
        result|=(uint32_t)lround(top*(1-fy)+bottom*fy)<<(k*8);
    }
    return result;
}
static int check(io_connect_t connection,const char *name,unsigned sw,unsigned sh,unsigned dw,unsigned dh,int linear,int crop)
{
    IOSurfaceRef a=surface(sw,sh),b=surface(dw,dh);
    if(!a||!b) { printf("displaytest: %s surface allocation failed\n",name);return 1; }
    IOSurfaceLock(a,0,NULL);IOSurfaceLock(b,0,NULL);
    size_t ap=IOSurfaceGetBytesPerRow(a),bp=IOSurfaceGetBytesPerRow(b);
    uint8_t *src=IOSurfaceGetBaseAddress(a),*dst=IOSurfaceGetBaseAddress(b);
    for(unsigned y=0;y<sh;y++)for(unsigned x=0;x<sw;x++)((uint32_t *)(src+y*ap))[x]=pixel(x,y,sw,sh);
    memset(dst,0xa5,bp*dh);
    IOSurfaceUnlock(b,0,NULL);IOSurfaceUnlock(a,0,NULL);
    struct blit r={.source=IOSurfaceGetID(a),.destination=IOSurfaceGetID(b),.src={0,0,sw,sh},.dst={0,0,dw,dh},.linear=linear};
    if (crop==2) r.source=0;
    if(crop) { int32_t sr[4]={8,5,16,16},dr[4]={5,3,32,32};memcpy(r.src,sr,sizeof(sr));memcpy(r.dst,dr,sizeof(dr)); }
    size_t size=sizeof(r);uint64_t start=mach_absolute_time();
    kern_return_t kr=IOConnectCallStructMethod(connection,0x7c,&r,sizeof(r),&r,&size);
    uint64_t elapsed=mach_absolute_time()-start;mach_timebase_info_data_t tb;mach_timebase_info(&tb);
    unsigned bad=0,maxdelta=0;
    if(kr==KERN_SUCCESS) {
        IOSurfaceLock(b,kIOSurfaceLockReadOnly,NULL);
        for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<bp/4;x++) {
            uint32_t actual=((uint32_t *)(dst+y*bp))[x],want=x<dw?reference(x,y,sw,sh,&r):0xa5a5a5a5;
            unsigned delta=0;
            for(unsigned k=0;k<4;k++) { int d=(int)((actual>>(k*8))&255)-(int)((want>>(k*8))&255);unsigned v=abs(d);if(v>delta)delta=v; }
            if(delta>maxdelta)maxdelta=delta;
            int inside=x>=(unsigned)r.dst[0] && y>=(unsigned)r.dst[1] &&
                       x<(unsigned)(r.dst[0]+r.dst[2]) && y<(unsigned)(r.dst[1]+r.dst[3]);
            if(delta>(linear && inside?1u:0u)) { if(bad<4)printf("displaytest: %s pixel %u,%u got=%08x expected=%08x\n",name,x,y,actual,want);bad++; }
        }
        IOSurfaceUnlock(b,kIOSurfaceLockReadOnly,NULL);
    }
    printf("displaytest: %s kr=%#x fence=%u bad=%u maxdelta=%u time_us=%llu\n",name,kr,r.fence,bad,maxdelta,elapsed*tb.numer/tb.denom/1000);
    CFRelease(a);CFRelease(b);
    return kr!=KERN_SUCCESS || bad!=0 || r.fence==0;
}
int main(void)
{
    setbuf(stdout,NULL);
    io_service_t service=IOServiceGetMatchingService(kIOMainPortDefault,IOServiceMatching("RedwoodGPU"));
    io_connect_t connection=0;
    if(!service||IOServiceOpen(service,mach_task_self(),0,&connection)) { puts("displaytest: cannot open RedwoodGPU");return 1; }
    IOObjectRelease(service);
    int failures=check(connection,"identity",64,32,64,32,0,0);
    failures+=check(connection,"nearest-2x",64,32,128,64,0,0);
    failures+=check(connection,"linear-2x",64,32,128,64,1,0);
    failures+=check(connection,"cropped-nearest",64,32,63,47,0,1);
    failures+=check(connection,"cropped-linear",64,32,63,47,1,1);
    failures+=check(connection,"panel-nearest",1125,2436,1264,2780,0,0);
    failures+=check(connection,"panel-linear",1125,2436,1264,2780,1,0);
    failures+=check(connection,"cropped-clear",64,32,63,47,0,2);
    IOServiceClose(connection);
    printf("displaytest: COMPLETE failures=%d\n",failures);return failures?1:0;
}
