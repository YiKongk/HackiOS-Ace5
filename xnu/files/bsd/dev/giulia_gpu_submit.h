/* A7xx command framing: Linux v6.17 a7xx_submit/a6xx_set_pagetable and
 * UniXNU NativeAdrenoRing (b686b905). The fixed address space does not switch
 * TTBRs; explicit cache flush/invalidate and the SRAM-init wait cover that
 * part of the CP_SMMU_TABLE_UPDATE sequence. No IRQ is requested yet. */
static uint32_t a750_pm4(unsigned opcode, unsigned count)
{
    return 0x70000000|count|((1u^__builtin_parity(count))<<15)|
        (opcode<<16)|((1u^__builtin_parity(opcode))<<23);
}
static uint32_t a750_pm4_reg(unsigned reg)
{
    return 0x40000001|(reg<<8)|((1u^__builtin_parity(reg))<<27);
}
static unsigned a750_submit_words(uint32_t *p, unsigned capacity,
    const uint64_t *addresses, const uint32_t *sizes, unsigned count,
    uint32_t previous, uint32_t fence, bool reset)
{
    if (!p || !addresses || !sizes || !count || count>128 || !fence) return 0;
    unsigned wanted=11+(reset?26:0)+4*count+31;
    if (capacity<wanted) return 0;
    for (unsigned i=0;i<count;i++)
        if (!addresses[i] || (addresses[i]&3) || addresses[i]>=0x100000000ull ||
            !sizes[i] || sizes[i]>=1<<20 || addresses[i]+4ull*sizes[i]>0x100000000ull) return 0;
    unsigned n=0;
#define A750_P1(op,v) do { p[n++]=a750_pm4(op,1);p[n++]=(v); } while (0)
#define A750_P4(op,a,b,c,d) do { p[n++]=a750_pm4(op,4);p[n++]=(a);p[n++]=(b);p[n++]=(c);p[n++]=(d); } while (0)
    A750_P1(0x17,0x80000001); /* synchronize, BR only */
    if (reset) {
        A750_P4(0x14,0,0x61048004,0,previous);
        A750_P1(0x17,0x80000003);
        A750_P1(0x1f,0xf); /* context synchronization state */
        A750_P1(0x17,0x80000003);
        A750_P1(0x46,0x26); /* LRZ_FLUSH_INVALIDATE */
        A750_P1(0x17,0x80000001);
        p[n++]=a750_pm4_reg(0x50e);p[n++]=1;
        A750_P1(0x46,0x31);
        p[n++]=a750_pm4(0x3c,6); /* WAIT_REG_MEM: SRAM init has finished */
        p[n++]=3;p[n++]=0x50f;p[n++]=0;p[n++]=1;p[n++]=1;p[n++]=0;
    }
    A750_P1(0x46,0x32); /* CACHE_FLUSH7 */
    A750_P1(0x46,0x33); /* CACHE_INVALIDATE7 */
    p[n++]=a750_pm4(0x26,0); /* WAIT_FOR_IDLE */
    A750_P1(0x17,3);
    A750_P1(0x65,0x101); /* disable IFPC around the submission */
    for (unsigned i=0;i<count;i++) {
        p[n++]=a750_pm4(0x3f,3);p[n++]=(uint32_t)addresses[i];
        p[n++]=0;p[n++]=sizes[i];
    }
    p[n++]=a750_pm4_reg(0x885);p[n++]=fence;
    A750_P1(0x17,1);
    A750_P1(0x46,0x18); /* CCU_INVALIDATE_DEPTH */
    A750_P1(0x46,0x19); /* CCU_INVALIDATE_COLOR */
    A750_P1(0x17,2);
    A750_P4(0x46,0x08000031,0x6104800c,0,fence); /* BV completion */
    A750_P1(0x17,1);
    A750_P4(0x14,0,0x6104800c,0,fence); /* BR waits for BV */
    A750_P4(0x46,0x08000031,0x61048004,0,fence); /* ring completion */
    A750_P1(0x17,3);
    A750_P1(0x65,0x100);
#undef A750_P1
#undef A750_P4
    return n==wanted?n:0;
}
