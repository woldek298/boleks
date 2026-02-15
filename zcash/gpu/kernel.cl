#pragma OPENCL EXTENSION cl_khr_global_int32_base_atomics : enable


#define EVEN(x) (!(x & 0x1))
#define ODD(x)  (x & 0x1)

typedef struct {
  uint i0;
  uint i1;
  uint i2;
  uint i3;
  uint i4;
  uint i5;
  uint i6;
  uint ref;
} slot_t;

uint get_collision_data(uint xi0, uint round)
{
  uint mask;
  uint mask2;
  uint shift;
  uint shift2;
  
  // xi0 original data layout: y1 y4 y3 y2 x4 y0 x3 x2 x1 x0
  // xi0 even round:                 y1 y4 y3 y2 x4 y0 x3 x2
  // xi0 odd round:                        y1 y4 y3 y2 x4 y0
  
#if NR_ROWS_LOG == 10
  return EVEN(round) ?
    ((xi0 & 0xF000) >> 12) | (xi0 & 0xF0) | ((xi0 & 0xC) << 6) :
    ((xi0 & 0xF0000) >> 16) | ((xi0 & 0xF000) >> 8) | ((xi0 & 0xC00) >> 2);  
#elif NR_ROWS_LOG == 11
  return EVEN(round) ?
    ((xi0 & 0xF000) >> 12) | (xi0 & 0xF0) | ((xi0 & 0x8) << 5) :
    ((xi0 & 0xF0000) >> 16) | ((xi0 & 0xF000) >> 8) | ((xi0 & 0x800) >> 3);
#elif NR_ROWS_LOG == 12
  return EVEN(round) ?
    ((xi0 & 0xF000) >> 12) | (xi0 & 0xF0) :
    ((xi0 & 0xF0000) >> 16) | ((xi0 & 0xF000) >> 8);
#elif NR_ROWS_LOG == 13
  return EVEN(round) ?
    ((xi0 & 0xF000) >> 12) | ((xi0 & 0xE0) >> 1) :
    ((xi0 & 0xF0000) >> 16) | ((xi0 & 0xE000) >> 9);
#elif NR_ROWS_LOG == 14
  return EVEN(round) ?
    ((xi0 & 0xF000) >> 12) | ((xi0 & 0xC0) >> 2) :
    ((xi0 & 0xF0000) >> 16) | ((xi0 & 0xC000) >> 10);
#elif NR_ROWS_LOG == 15
  return EVEN(round) ?
    ((xi0 & 0xF000) >> 12) | ((xi0 & 0x80) >> 3) :
    ((xi0 & 0xF0000) >> 16) | ((xi0 & 0x8000) >> 11);
#elif NR_ROWS_LOG == 16
  return EVEN(round) ?
    ((xi0 & 0xF000) >> 12) :
    ((xi0 & 0xF0000) >> 16);
#endif
}

void slot_shr(slot_t *slot, uint n)
{
  slot->i0 = (slot->i0 >> n) | (slot->i1 << (32-n));
  slot->i1 = (slot->i1 >> n) | (slot->i2 << (32-n));
  slot->i2 = (slot->i2 >> n) | (slot->i3 << (32-n));
  slot->i3 = (slot->i3 >> n) | (slot->i4 << (32-n));
  slot->i4 = (slot->i4 >> n) | (slot->i5 << (32-n));
  slot->i5 = (slot->i5 >> n) | (slot->i6 << (32-n));
}

void find_slot_std(__global char *ht,
                   uint round,
                   uint rowIdx,
                   uint slotIdx,
                   __global char **lp,
                   __global char **hp)
{
#ifdef NV_L2CACHE_OPT
  const uint SlotsInRow = 1 << NV_L2CACHE_OPT;
  const uint SlotMask = (1 << NV_L2CACHE_OPT) - 1;  
  uint rowOffset = ((slotIdx >> NV_L2CACHE_OPT)*NR_ROWS*SLOT_LEN(round)*SlotsInRow) + (rowIdx*SLOT_LEN(round)*SlotsInRow);
  uint slotOffset = slotIdx & SlotMask;
#else
  uint rowOffset = rowIdx*NR_SLOTS*SLOT_LEN(round);
  uint slotOffset = slotIdx;
#endif
  *lp = ht + rowOffset + SLOT_LEN(round)*slotOffset;
}

void find_slot_24(__global char *ht,
                  uint round,
                  uint rowIdx,
                  uint slotIdx,
                  __global char **lp,
                  __global char **hp)
{
#ifdef NV_L2CACHE_OPT
  const uint SlotsInRow = 1 << NV_L2CACHE_OPT;
  const uint SlotMask = (1 << NV_L2CACHE_OPT) - 1;  
  uint rowOffset = ((slotIdx >> NV_L2CACHE_OPT)*NR_ROWS*SLOT_LEN(round)*SlotsInRow) + (rowIdx*SLOT_LEN(round)*SlotsInRow);
  uint slotOffset = slotIdx & SlotMask;
#else
  uint rowOffset = rowIdx*NR_SLOTS*SLOT_LEN(round);
  uint slotOffset = slotIdx;
#endif
  __global char *p = ht + rowOffset + SLOT_LEN(round)*slotOffset;
  uint loffset = (slotIdx & 0x1) ? 8 : 0;
  uint hoffset = (slotIdx & 0x1) ? 0 : 16;
  *lp = p + loffset;
  *hp = p + hoffset;
}

void find_slot_4_x(__global char *ht,
                   uint round,
                   uint rowIdx,
                   uint slotIdx,
                   __global char **lp,
                   __global char **hp)
{
  
}

uint expand_ref(__global char *ht, uint round, uint rowIdx, uint slotIdx)
{
  __global char *lp;
  __global char *hp;  
  if (round == 0 ||
      round == 1) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    return *(__global uint*)(lp + 4*UINTS_IN_XI(round));
  } else if (round == 2 ||
             round == 3 ||
             round == 4 ||
             round == 5) {
    find_slot_24(ht, round, rowIdx, slotIdx, &lp, &hp);
    return *(__global uint*)(hp + 4*(UINTS_IN_XI(round)-4));
  } else if (round == 6 ||
             round == 7) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    return *(__global uint*)(lp + 4*UINTS_IN_XI(round));
  } else if (round == 8) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    return *(__global uint*)(lp + 4*UINTS_IN_XI(round));
  }  
}

// slot_t read_slot_global(__global char *p, uint round)
slot_t read_slot_global(__global char *ht, uint rowIdx, uint slotIdx, uint round)
{
  slot_t out;
  
  __global char *lp;
  __global char *hp;  
  if (round == 0 ||
      round == 1) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    uint8 l = *(__global uint8*)lp;
    out.i0 = l.s0;
    out.i1 = l.s1;
    out.i2 = l.s2;
    out.i3 = l.s3;
    out.i4 = l.s4;
    out.i5 = l.s5;   
  } else if (round == 2 ||
             round == 3) {
    find_slot_24(ht, round, rowIdx, slotIdx, &lp, &hp);
    uint4 l = *(__global uint4*)lp;
    uint2 h = *(__global uint2*)hp;  
    out.i0 = l.s0;
    out.i1 = l.s1;
    out.i2 = l.s2;
    out.i3 = l.s3;
    out.i4 = h.s0;
    out.i5 = h.s1;
  } else if (round == 4 ||
             round == 5) {
    find_slot_24(ht, round, rowIdx, slotIdx, &lp, &hp);
    uint4 l = *(__global uint4*)lp;
    uint h = *(__global uint*)hp;  
    out.i0 = l.s0;
    out.i1 = l.s1;
    out.i2 = l.s2;
    out.i3 = l.s3;
    out.i4 = h;
  }  else if (round == 6 ||
             round == 7) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    uint4 l = *(__global uint4*)lp;  
    out.i0 = l.s0;
    out.i1 = l.s1;
    out.i2 = l.s2;
    out.i3 = l.s3;  
  } else if (round == 8) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    uint2 l = *(__global uint2*)lp;  
    out.i0 = l.s0;
    out.i1 = l.s1; 
  }  

  return out;
}

void write_slot_global(__global char *ht, __global uint *rowCounters, slot_t in, uint round)
{
  // calculate row number
  // xi0 original data layout: y1 y4 y3 y2 x4 y0 x3 x2 x1 x0
  // xi0 even round:           y1 y4 y3 y2 x4 y0 x3 x2 x1 x0
  // xi0 odd round:                  y1 y4 y3 y2 x4 y0 x3 x2
  uint rowIdx;
#if NR_ROWS_LOG == 10
 rowIdx = EVEN(round) ? in.i0 & 0x3ff : ((in.i0 & 0x030f00) >> 8) | ((in.i0 & 0xf0000000) >> 24);  
#elif NR_ROWS_LOG == 11
 rowIdx = EVEN(round) ? in.i0 & 0x7ff : ((in.i0 & 0x070f00) >> 8) | ((in.i0 & 0xf0000000) >> 24);
#elif NR_ROWS_LOG == 12
 rowIdx = EVEN(round) ? in.i0 & 0xfff : ((in.i0 & 0x0f0f00) >> 8) | ((in.i0 & 0xf0000000) >> 24);
#elif NR_ROWS_LOG == 13
  rowIdx = EVEN(round) ? in.i0 & 0x1fff : ((in.i0 & 0x1f0f00) >> 8) | ((in.i0 & 0xf0000000) >> 24);  
#elif NR_ROWS_LOG == 14
  rowIdx = EVEN(round) ? in.i0 & 0x3fff : ((in.i0 & 0x3f0f00) >> 8) | ((in.i0 & 0xf0000000) >> 24);    
#elif NR_ROWS_LOG == 15
  rowIdx = EVEN(round) ? in.i0 & 0x7fff : ((in.i0 & 0x7f0f00) >> 8) | ((in.i0 & 0xf0000000) >> 24);    
#elif NR_ROWS_LOG == 16
  rowIdx = EVEN(round) ? in.i0 & 0xffff : ((in.i0 & 0xff0f00) >> 8) | ((in.i0 & 0xf0000000) >> 24);       
#else
#error "unsupported NR_ROWS_LOG"
#endif

  // calculate slot position in row
  uint slotIdx;
  uint rowOffset = BITS_PER_ROW * (rowIdx % ROWS_PER_UINT);
  slotIdx = atomic_add(rowCounters + rowIdx/ROWS_PER_UINT, 1 << rowOffset);
  slotIdx = (slotIdx >> rowOffset) & ROW_MASK;
  slotIdx = min(slotIdx, NR_SLOTS-1);
  
  // remove padding
  slot_shr(&in, 8);
  
  // store data
  __global char *lp;
  __global char *hp;  
  if (round == 0) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint8*)lp = (uint8){in.i0, in.i1, in.i2, in.i3, in.i4, in.i5, in.ref, 0};
  } else if (round == 1) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint8*)lp = (uint8){in.i0, in.i1, in.i2, in.i3, in.i4, in.i5, in.ref, 0};
  } else if (round == 2) {
    find_slot_24(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint4*)lp = (uint4){in.i0, in.i1, in.i2, in.i3};
    *(__global uint2*)hp = (uint2){in.i4, in.ref};
  } else if (round == 3) {
    find_slot_24(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint4*)lp = (uint4){in.i0, in.i1, in.i2, in.i3};
    *(__global uint2*)hp = (uint2){in.i4, in.ref};
  } else if (round == 4) {
    find_slot_24(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint4*)lp = (uint4){in.i0, in.i1, in.i2, in.i3};
    *(__global uint*)hp = in.ref;
  } else if (round == 5) {
    find_slot_24(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint4*)lp = (uint4){in.i0, in.i1, in.i2, in.i3};
    *(__global uint*)hp = in.ref;
  } else if (round == 6) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint4*)lp = (uint4){in.i0, in.i1, in.i2, in.ref};    
  } else if (round == 7) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint4*)lp = (uint4){in.i0, in.i1, in.ref, 0};    
  } else if (round == 8) {
    find_slot_std(ht, round, rowIdx, slotIdx, &lp, &hp);
    *(__global uint2*)lp = (uint2){in.i0, in.ref};    
  }
}

slot_t read_slot_local(__local uint *in, uint slotIdx, uint round)
{
  slot_t out;
  if (UINTS_IN_XI(round) == 6) {
    // 4 + 2
    uint4 l = *(__local uint4*)(in + 4*slotIdx);
    uint2 h = *(__local uint2*)(in + 4*NR_SLOTS + 2*slotIdx);
    out.i0 = l.s0;
    out.i1 = l.s1;
    out.i2 = l.s2;
    out.i3 = l.s3;    
    out.i4 = h.s0;
    out.i5 = h.s1;
  } else if (UINTS_IN_XI(round) == 5) {
    // 4 + 1
    uint4 l = *(__local uint4*)(in + 4*slotIdx);
    uint h = *(in + 4*NR_SLOTS + slotIdx);
    out.i0 = l.s0;
    out.i1 = l.s1;
    out.i2 = l.s2;
    out.i3 = l.s3;    
    out.i4 = h;
  } else if (UINTS_IN_XI(round) == 4) {
    // 4
    uint4 l = *(__local uint4*)(in + 4*slotIdx);
    out.i0 = l.s0;
    out.i1 = l.s1;
    out.i2 = l.s2;
    out.i3 = l.s3;    
  } else if (UINTS_IN_XI(round) == 3) {
    // 2 + 1
    uint2 l = *(__local uint2*)(in + 2*slotIdx);
    uint h = *(in + 2*NR_SLOTS + slotIdx);
    out.i0 = l.s0;
    out.i1 = l.s1;
    out.i2 = h;      
  } else if (UINTS_IN_XI(round) == 2) {
    // 2
    uint2 l = *(__local uint2*)(in + 2*slotIdx);
    out.i0 = l.s0;
    out.i1 = l.s1;    
  } else if (UINTS_IN_XI(round) == 1) {
    // 1
    out.i0 = *(in + slotIdx);
  }
  
  return out;
}


void write_slot_local(slot_t in, __local uint *out, uint slotIdx, uint round)
{
  if (UINTS_IN_XI(round) == 6) {
    // 4 + 2
    uint4 l = {in.i0, in.i1, in.i2, in.i3};
    uint2 h = {in.i4, in.i5};
    *(__local uint4*)(out + 4*slotIdx) = l;
    *(__local uint2*)(out + 4*NR_SLOTS + 2*slotIdx) = h;
  } else  if (UINTS_IN_XI(round) == 5) {
    // 4 + 1
    uint4 l = {in.i0, in.i1, in.i2, in.i3};
    uint h = in.i4;
    *(__local uint4*)(out + 4*slotIdx) = l;
    *(out + 4*NR_SLOTS + slotIdx) = h;
  } else  if (UINTS_IN_XI(round) == 4) {
    // 4
    uint4 l = {in.i0, in.i1, in.i2, in.i3};
    *(__local uint4*)(out + 4*slotIdx) = l;
  } else  if (UINTS_IN_XI(round) == 3) {
    // 2 + 1
    uint2 l = {in.i0, in.i1};
    uint h = in.i2;
    *(__local uint2*)(out + 2*slotIdx) = l;
    *(out + 2*NR_SLOTS + slotIdx) = h;
  } else  if (UINTS_IN_XI(round) == 2) {
    // 2
    uint2 l = {in.i0, in.i1};
    *(__local uint2*)(out + 2*slotIdx) = l;
  } else  if (UINTS_IN_XI(round) == 1) {
    // 1
    *(__local uint*)(out + slotIdx) = in.i0;
  }
}

/*
** Assuming NR_ROWS_LOG == 16, the hash table slots have this layout (length in
** bytes in parens):
**
** round 0, table 0: cnt(4) i(4)                     pad(0)   Xi(23.0) pad(1)
** round 1, table 1: cnt(4) i(4)                     pad(0.5) Xi(20.5) pad(3)
** round 2, table 0: cnt(4) i(4) i(4)                pad(0)   Xi(18.0) pad(2)
** round 3, table 1: cnt(4) i(4) i(4)                pad(0.5) Xi(15.5) pad(4)
** round 4, table 0: cnt(4) i(4) i(4) i(4)           pad(0)   Xi(13.0) pad(3)
** round 5, table 1: cnt(4) i(4) i(4) i(4)           pad(0.5) Xi(10.5) pad(5)
** round 6, table 0: cnt(4) i(4) i(4) i(4) i(4)      pad(0)   Xi( 8.0) pad(4)
** round 7, table 1: cnt(4) i(4) i(4) i(4) i(4)      pad(0.5) Xi( 5.5) pad(6)
** round 8, table 0: cnt(4) i(4) i(4) i(4) i(4) i(4) pad(0)   Xi( 3.0) pad(5)
**
** If the first byte of Xi is 0xAB then:
** - on even rounds, 'A' is part of the colliding PREFIX, 'B' is part of Xi
** - on odd rounds, 'A' and 'B' are both part of the colliding PREFIX, but
**   'A' is considered redundant padding as it was used to compute the row #
**
** - cnt is an atomic counter keeping track of the number of used slots.
**   it is used in the first slot only; subsequent slots replace it with
**   4 padding bytes
** - i encodes either the 21-bit input value (round 0) or a reference to two
**   inputs from the previous round
**
** Formula for Xi length and pad length above:
** > for i in range(9):
** >   xi=(200-20*i-NR_ROWS_LOG)/8.; ci=8+4*((i)/2); print xi,32-ci-xi
**
** Note that the fractional .5-byte/4-bit padding following Xi for odd rounds
** is the 4 most significant bits of the last byte of Xi.
*/


__constant ulong blake_iv[] =
{
    0x6a09e667f3bcc908, 0xbb67ae8584caa73b,
    0x3c6ef372fe94f82b, 0xa54ff53a5f1d36f1,
    0x510e527fade682d1, 0x9b05688c2b3e6c1f,
    0x1f83d9abfb41bd6b, 0x5be0cd19137e2179,
};

/*
** Reset counters in hash table.
*/
__kernel
void kernel_init_ht(__global char *ht, __global uint *rowCounters)
{
    rowCounters[get_global_id(0)] = 0;
}


/*
** If xi0,xi1,xi2,xi3 are stored consecutively in little endian then they
** represent (hex notation, group of 5 hex digits are a group of PREFIX bits):
**   aa aa ab bb bb cc cc cd dd...  [round 0]
**         --------------------
**      ...ab bb bb cc cc cd dd...  [odd round]
**               --------------
**               ...cc cc cd dd...  [next even round]
**                        -----
** Bytes underlined are going to be stored in the slot. Preceding bytes
** (and possibly part of the underlined bytes, depending on NR_ROWS_LOG) are
** used to compute the row number.
**
** Round 0: xi0,xi1,xi2,xi3 is a 25-byte Xi (xi3: only the low byte matter)
** Round 1: xi0,xi1,xi2 is a 23-byte Xi (incl. the colliding PREFIX nibble)
** TODO: update lines below with padding nibbles
** Round 2: xi0,xi1,xi2 is a 20-byte Xi (xi2: only the low 4 bytes matter)
** Round 3: xi0,xi1,xi2 is a 17.5-byte Xi (xi2: only the low 1.5 bytes matter)
** Round 4: xi0,xi1 is a 15-byte Xi (xi1: only the low 7 bytes matter)
** Round 5: xi0,xi1 is a 12.5-byte Xi (xi1: only the low 4.5 bytes matter)
** Round 6: xi0,xi1 is a 10-byte Xi (xi1: only the low 2 bytes matter)
** Round 7: xi0 is a 7.5-byte Xi (xi0: only the low 7.5 bytes matter)
** Round 8: xi0 is a 5-byte Xi (xi0: only the low 5 bytes matter)
**
** Return 0 if successfully stored, or 1 if the row overflowed.
*/

#define mix(va, vb, vc, vd, x, y) \
    va = (va + vb + x); \
vd = rotate((vd ^ va), (ulong)64 - 32); \
vc = (vc + vd); \
vb = rotate((vb ^ vc), (ulong)64 - 24); \
va = (va + vb + y); \
vd = rotate((vd ^ va), (ulong)64 - 16); \
vc = (vc + vd); \
vb = rotate((vb ^ vc), (ulong)64 - 63);

/*
** Execute round 0 (blake).
**
** Note: making the work group size less than or equal to the wavefront size
** allows the OpenCL compiler to remove the barrier() calls, see "2.2 Local
** Memory (LDS) Optimization 2-10" in:
** http://developer.amd.com/tools-and-sdks/opencl-zone/amd-accelerated-parallel-processing-app-sdk/opencl-optimization-guide/
*/
__kernel __attribute__((reqd_work_group_size(BLAKE_WORKGROUP_SIZE, 1, 1)))
void kernel_round0(__global char *ht,
                   __global uint *rowCounters,
                   __global uint *debug,
                   ulong d0,
                   ulong d1,
                   ulong d2,
                   ulong d3,
                   ulong d4,
                   ulong d5,
                   ulong d6,
                   ulong d7)
{
    uint                tid = get_global_id(0);
    ulong               v[16];
    uint                inputs_per_thread = NR_INPUTS / get_global_size(0);
    uint                input = tid * inputs_per_thread;
    uint                input_end = (tid + 1) * inputs_per_thread;
    uint                dropped = 0;
    while (input < input_end)
      {
  // shift "i" to occupy the high 32 bits of the second ulong word in the
  // message block
  ulong word1 = (ulong)input << 32;
  // init vector v
  v[0] = d0;
  v[1] = d1;
  v[2] = d2;
  v[3] = d3;
  v[4] = d4;
  v[5] = d5;
  v[6] = d6;
  v[7] = d7;
  v[8] =  blake_iv[0];
  v[9] =  blake_iv[1];
  v[10] = blake_iv[2];
  v[11] = blake_iv[3];
  v[12] = blake_iv[4];
  v[13] = blake_iv[5];
  v[14] = blake_iv[6];
  v[15] = blake_iv[7];
  // mix in length of data
  v[12] ^= ZCASH_BLOCK_HEADER_LEN + 4 /* length of "i" */;
  // last block
  v[14] ^= (ulong)-1;

  // round 1
  mix(v[0], v[4], v[8],  v[12], 0, word1);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 2
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], word1, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 3
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, word1);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 4
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, word1);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 5
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, word1);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 6
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], word1, 0);
  // round 7
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], word1, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 8
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, word1);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 9
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], word1, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 10
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], word1, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 11
  mix(v[0], v[4], v[8],  v[12], 0, word1);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], 0, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);
  // round 12
  mix(v[0], v[4], v[8],  v[12], 0, 0);
  mix(v[1], v[5], v[9],  v[13], 0, 0);
  mix(v[2], v[6], v[10], v[14], 0, 0);
  mix(v[3], v[7], v[11], v[15], 0, 0);
  mix(v[0], v[5], v[10], v[15], word1, 0);
  mix(v[1], v[6], v[11], v[12], 0, 0);
  mix(v[2], v[7], v[8],  v[13], 0, 0);
  mix(v[3], v[4], v[9],  v[14], 0, 0);

  // compress v into the blake state; this produces the 50-byte hash
  // (two Xi values)
  ulong h[7];
  h[0] = d0 ^ v[0] ^ v[8];
  h[1] = d1 ^ v[1] ^ v[9];
  h[2] = d2 ^ v[2] ^ v[10];
  h[3] = d3 ^ v[3] ^ v[11];
  h[4] = d4 ^ v[4] ^ v[12];
  h[5] = d5 ^ v[5] ^ v[13];
  h[6] = (d6 ^ v[6] ^ v[14]) & 0xffff;

  // store the two Xi values in the hash table
#if ZCASH_HASH_LEN == 50
  {
    slot_t slot;
    slot.ref = input*2;
    slot.i0 = h[0];
    slot.i1 = h[0] >> 32;
    slot.i2 = h[1];
    slot.i3 = h[1] >> 32;
    slot.i4 = h[2];
    slot.i5 = h[2] >> 32;  
    slot.i6 = h[3];
    write_slot_global(ht, rowCounters, slot, 0);
  }
  
  {
    slot_t slot;
    slot.ref = input*2 + 1;
    ulong ul0 = (h[3] >> 8) | (h[4] << (64 - 8));
    ulong ul1 = (h[4] >> 8) | (h[5] << (64 - 8));
    ulong ul2 = (h[5] >> 8) | (h[6] << (64 - 8));
    ulong ul3 = (h[6] >> 8);
    slot.i0 = ul0;
    slot.i1 = ul0 >> 32;
    slot.i2 = ul1;
    slot.i3 = ul1 >> 32;
    slot.i4 = ul2;
    slot.i5 = ul2 >> 32;  
    slot.i6 = ul3;
    write_slot_global(ht, rowCounters, slot, 0);
  }

#else
#error "unsupported ZCASH_HASH_LEN"
#endif

  input++;
      }
#ifdef ENABLE_DEBUG
    debug[tid * 2] = 0;
    debug[tid * 2 + 1] = dropped;
#endif
}

uint encode_ref(uint row, uint slot0, uint slot1)
{
#if (NR_ROWS_LOG == 16) || (NR_ROWS_LOG == 15)
  return (row << 16) | ((slot1 & 0xFF) << 8) | (slot0 & 0xFF);
#elif (NR_ROWS_LOG == 14) || (NR_ROWS_LOG == 13)
  return (row << 18) | ((slot1 & 0x1FF) << 9) | (slot0 & 0x1FF);
#elif NR_ROWS_LOG == 12
  return (row << 20) | ((slot1 & 0x3FF) << 10) | (slot0 & 0x3FF);
#elif NR_ROWS_LOG == 11
  // sloth takes 5 bit
  uint slot0h = slot0 >> 8;
  uint slot1h = slot1 >> 8;
  uint sloth = slot1h*5 + slot0h;
  return (row << 21) | (sloth << 16) | ((slot1 & 0xFF) << 8) | (slot0 & 0xFF);
#else
#error "unsupported NR_ROWS_LOG"
#endif
}

void decode_ref(uint ref, uint *row, uint *slot0, uint *slot1)
{
#if (NR_ROWS_LOG == 16) || (NR_ROWS_LOG == 15)  
  *row = ref >> 16;
  *slot1 = (ref >> 8) & 0xFF;
  *slot0 = ref & 0xFF;  
#elif (NR_ROWS_LOG == 14) || (NR_ROWS_LOG == 13)  
  *row = ref >> 18;
  *slot1 = (ref >> 9) & 0x1FF;
  *slot0 = ref & 0x1FF;
#elif NR_ROWS_LOG == 12
  *row = ref >> 20;
  *slot1 = (ref >> 10) & 0x3FF;
  *slot0 = ref & 0x3FF;
#elif NR_ROWS_LOG == 11
  uint sloth = (ref >> 16) & 0x1F;
  uint slot1h = sloth / 5;
  uint slot0h = sloth % 5;
  *row = ref >> 21;
  *slot1 = ((ref >> 8) & 0xFF) | (slot1h << 8);
  *slot0 = (ref & 0xFF) | (slot0h << 8);
#else
#error "unsupported NR_ROWS_LOG"
#endif
}

uint get_slots_number(__global uint *rowCounters, uint rowIdx)
{
  uint slotsInRow;
  slotsInRow = (rowCounters[rowIdx/ROWS_PER_UINT] >> (BITS_PER_ROW*(rowIdx%ROWS_PER_UINT))) & ROW_MASK;
  slotsInRow = min(slotsInRow, (uint)NR_SLOTS);
  return slotsInRow;
}

/*
** Execute one Equihash round. Read from ht_src, XOR colliding pairs of Xi,
** store them in ht_dst.
*/
void equihash_round(uint round,
                    __global char *ht_src,
                    __global char *ht_dst,
                    __global uint *debug,
                    __local uint *slots,
                    __global uint *rowCountersSrc,
                    __global uint *rowCountersDst)
{
  uint rowIdx = get_group_id(0);
  uint localTid = get_local_id(0);
  __local uint slotCountersData[COLLISION_PER_ROW];
  __local ushort slotsData[COLLISION_PER_ROW*COLLISION_BUFFER_SIZE];

  uint slotsInRow = get_slots_number(rowCountersSrc, rowIdx);

  for (uint i = get_local_id(0); i < COLLISION_PER_ROW; i += get_local_size(0))
    slotCountersData[i] = 0;
  barrier(CLK_LOCAL_MEM_FENCE);
    
  for (uint i = localTid; i < slotsInRow; i += ROUND_WORKGROUP_SIZE) {
    // read slot
    slot_t slot = read_slot_global(ht_src, rowIdx, i, round-1);
    
    // cache in local memory
    write_slot_local(slot, slots, i, round-1);

    uint x = get_collision_data(slot.i0, round-1);    
    uint slotIdx = atomic_inc(&slotCountersData[x]);
    slotIdx = min(slotIdx, COLLISION_BUFFER_SIZE-1);
    slotsData[COLLISION_BUFFER_SIZE*x+slotIdx] = i;
  }

    barrier(CLK_LOCAL_MEM_FENCE);

  for (uint i = localTid; i < slotsInRow; i += ROUND_WORKGROUP_SIZE) {
    slot_t s = read_slot_local(slots, i, round-1);
    uint x = get_collision_data(s.i0, round-1);
    uint count = min(slotCountersData[x], COLLISION_BUFFER_SIZE-1);
    for (uint j = 0; j < count; j++) {
      uint collisionIdx = slotsData[COLLISION_BUFFER_SIZE*x + j];
      if (collisionIdx <= i)
        continue;      
      
      slot_t s1 = s;
      slot_t s2 = read_slot_local(slots, collisionIdx, round-1);

      // xor data and remove padding on even rounds
      s1.i0 ^= s2.i0;
      s1.i1 ^= s2.i1;
      s1.i2 ^= s2.i2;
      s1.i3 ^= s2.i3;
      s1.i4 ^= s2.i4;
      s1.i5 ^= s2.i5;    
      if (EVEN(round))
        slot_shr(&s1, 24);
    
      if (!s1.i0 && !s1.i1)
        continue;
    
      // store data
      s1.ref = encode_ref(rowIdx, i, collisionIdx); 
      write_slot_global(ht_dst, rowCountersDst, s1, round);
    }
  }
}

/*
** This defines kernel_round1, kernel_round2, ..., kernel_round7.
*/
#define KERNEL_ROUND(N) \
__kernel __attribute__((reqd_work_group_size(ROUND_WORKGROUP_SIZE, 1, 1))) \
void kernel_round ## N(__global char *ht_src, __global char *ht_dst, \
  __global uint *rowCountersSrc, __global uint *rowCountersDst, \
        __global uint *debug) \
{ \
    __local uint4 slots[UINTS_IN_XI(N-1) * NR_SLOTS / 4]; \
    equihash_round(N, ht_src, ht_dst, debug, (__local uint*)slots, rowCountersSrc, rowCountersDst); \
}
KERNEL_ROUND(1)
KERNEL_ROUND(2)
KERNEL_ROUND(3)
KERNEL_ROUND(4)
KERNEL_ROUND(5)
KERNEL_ROUND(6)
KERNEL_ROUND(7)

// kernel_round8 takes an extra argument, "sols"
__kernel __attribute__((reqd_work_group_size(ROUND_WORKGROUP_SIZE, 1, 1)))
void kernel_round8(__global char *ht_src, __global char *ht_dst,
  __global uint *rowCountersSrc, __global uint *rowCountersDst,
  __global uint *debug, __global potential_sols_t *potential_sols, __global sols_t *sols)
{
    uint    tid = get_global_id(0);
    __local uint4 slots[UINTS_IN_XI(8-1) * NR_SLOTS / 4];
    equihash_round(8, ht_src, ht_dst, debug, (__local uint*)slots, rowCountersSrc, rowCountersDst);
    if (!tid) {
      potential_sols->nr = 0;
      sols->nr = sols->likely_invalids = 0;
    }
}

__kernel __attribute__((reqd_work_group_size(ROUND_WORKGROUP_SIZE, 1, 1)))
void kernel_potential_sols(__global char *ht_src,
                           __global uint *rowCountersSrc,
                           __global potential_sols_t *sols)
{
  uint rowIdx = get_group_id(0);
  uint localTid = get_local_id(0);

  __local uint2 xi0WithRef[NR_SLOTS];
  __local uint slotCountersData[COLLISION_PER_ROW];
  __local ushort slotsData[COLLISION_PER_ROW*COLLISION_BUFFER_SIZE];

  uint slotsInRow = get_slots_number(rowCountersSrc, rowIdx);

  for (uint i = get_local_id(0); i < COLLISION_PER_ROW; i += get_local_size(0))
    slotCountersData[i] = 0;
  barrier(CLK_LOCAL_MEM_FENCE);
    
  for (uint i = localTid; i < slotsInRow; i += ROUND_WORKGROUP_SIZE) {
    // read xi0 and ref
    __global char *lp;
    __global char *hp;
    find_slot_std(ht_src, PARAM_K-1, rowIdx, i, &lp, &hp);
    uint2 slot = *(__global uint2*)lp;
    
    // cache in local memory
    xi0WithRef[i] = slot;
    
    uint collisionData = get_collision_data(slot.s0, PARAM_K-1);
    uint slotIdx = atomic_inc(&slotCountersData[collisionData]);
    slotIdx = min(slotIdx, COLLISION_BUFFER_SIZE-1);
    slotsData[COLLISION_BUFFER_SIZE*collisionData+slotIdx] = i;
  }
    
  barrier(CLK_LOCAL_MEM_FENCE);

  for (uint i = localTid; i < slotsInRow; i += ROUND_WORKGROUP_SIZE) {
    uint2 s = xi0WithRef[i];
    uint collisionData = get_collision_data(s.s0, PARAM_K-1);
    uint count = min(slotCountersData[collisionData], COLLISION_BUFFER_SIZE-1);
    for (uint j = 0; j < count; j++) {
      uint collisionIdx = slotsData[COLLISION_BUFFER_SIZE*collisionData + j];
      uint2 s1 = s; 
      uint2 s2 = xi0WithRef[collisionIdx];
      if (collisionIdx <= i || s1.s0 != s2.s0)
        continue;      

      uint sol_i = atomic_inc(&sols->nr);
      if (sol_i >= MAX_POTENTIAL_SOLS)
        return;
      sols->values[sol_i][0] = s1.s1;
      sols->values[sol_i][1] = s2.s1;
      break;
    }
  }
}

// Using "gateless gate" version

__kernel __attribute__((reqd_work_group_size(SOLS_WORKGROUP_SIZE, 1, 1)))
void kernel_sols(__global char *ht0,
                 __global char *ht1,
                 __global char *ht2,
                 __global char *ht3,
                 __global char *ht4,
                 __global char *ht5,
                 __global char *ht6,
                 __global char *ht7,
                 __global char *ht8,
                 __global sols_t *sols,
                 __global potential_sols_t *potential_sols)
{
  __local uint inputs_a[1 << PARAM_K];
  __local uint inputs_b[1 << (PARAM_K - 1)];
  
  __global char *htabs[] = { ht0, ht1, ht2, ht3, ht4, ht5, ht6, ht7, ht8 };

  if (get_group_id(0) < potential_sols->nr && get_group_id(0) < MAX_POTENTIAL_SOLS) {
    __local uint dup_counter;
    if (get_local_id(0) == 0) {
      dup_counter = 0;
      inputs_a[0] = potential_sols->values[get_group_id(0)][0];
      inputs_a[1] = potential_sols->values[get_group_id(0)][1];
    }
    
    barrier(CLK_LOCAL_MEM_FENCE);

    for (int round = 7; round >= 0; --round) {
      uint row;
      uint slot0;
      uint slot1;        
      if (round % 2) {      
        for (uint i = get_local_id(0); i < (1 << ((PARAM_K - 1) - round)); i += get_local_size(0)) {  
          decode_ref(inputs_a[i], &row, &slot0, &slot1);          
          inputs_b[i * 2 + 1] = expand_ref(htabs[round], round, row, slot1);
          inputs_b[i * 2] = expand_ref(htabs[round], round, row, slot0);
        }
      } else {
        for (uint i = get_local_id(0); i < (1 << ((PARAM_K - 1) - round)); i += get_local_size(0)) {
          decode_ref(inputs_b[i], &row, &slot0, &slot1);
          inputs_a[i * 2 + 1] = expand_ref(htabs[round], round, row, slot1);
          inputs_a[i * 2] = expand_ref(htabs[round], round, row, slot0);
        }
      }
      
      barrier(CLK_LOCAL_MEM_FENCE);
    }
    
    barrier(CLK_LOCAL_MEM_FENCE);

    int dup_to_watch = inputs_a[(1 << PARAM_K) - 1];
    
    for (uint i = get_local_id(0); i < (1 << (PARAM_K-1)); i += get_local_size(0)) {
      uint j = 3 + i;
      if (inputs_a[j] == dup_to_watch)
        atomic_inc(&dup_counter);
      j += (1 << (PARAM_K-1));
      if (j < (1 << PARAM_K) - 2 && inputs_a[j] == dup_to_watch)
        atomic_inc(&dup_counter);
    }
    
    barrier(CLK_LOCAL_MEM_FENCE);
        
    // solution appears valid, copy it to sols
    if (!dup_counter) {
      __local uint sol_i;
      if (!get_local_id(0))
        sol_i = atomic_inc(&sols->nr);
      barrier(CLK_LOCAL_MEM_FENCE);
      if (sol_i < MAX_SOLS) {
        if (!get_local_id(0))
          sols->valid[sol_i] = 1;
        for (uint i = get_local_id(0); i < (1 << PARAM_K); i += get_local_size(0))
          sols->values[sol_i][i] = inputs_a[i];
      }
    }
  }
}
