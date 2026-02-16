#define S1RUNS (sizeof(nps_all)/sizeof(uint32_t))
#define NLIFO 4

// for 1024 threads in group
#if (LSIZELOG2 == 10)
__constant__ uint32_t nps_all[] = { 4, 4, 5, 6, 7, 7, 7, 9 }; // 1024 threads per block (default)
#elif (LSIZELOG2 == 9)
__constant__ uint32_t nps_all[] = { 3, 3, 4, 5, 6, 6, 6, 7, 7 }; // 512 threads
#elif (LSIZELOG2 == 8)
__constant__ uint32_t nps_all[] = { 2, 2, 3, 4, 5, 5, 5, 6, 6 }; // 256 threads
#else
#error "Unsupported LSIZELOG2 constant"
#endif

__global__ void sieve(uint32_t *gsieve_all,
                      uint32_t* offset_all,
                      uint2 *primes)
{
  __shared__ uint32_t sieve[SIZE];
  
  const uint32_t id = threadIdx.x;
  const uint32_t stripe = blockIdx.x;
  const uint32_t line = blockIdx.y; 
  const uint32_t sieveBits = SIZE * 32;
  const uint32_t entry = sieveBits * (stripe + STRIPES/2);
  const float fentry = entry;
  
  const uint32_t* offset = &offset_all[PCOUNT*line];
  
  for (uint32_t i = id; i < SIZE; i += LSIZE)
    sieve[i] = 0;
  __syncthreads();
  
  uint32_t poff = 0;

#pragma unroll
  for(int b = 0; b < S1RUNS; b++) {
    uint32_t nps = nps_all[b];
    const uint32_t var = LSIZE >> nps;
    const uint32_t lpoff = id & (var-1);
    uint32_t ip = id >> (LSIZELOG2-nps);

    const uint2 tmp1 = primes[poff+ip];
    const uint32_t prime = tmp1.x;
    const float fiprime = __int_as_float(tmp1.y);

    const uint32_t loffset = offset[poff+ip];
    const uint32_t orb = (loffset >> 31) ^ 0x1;
    uint32_t pos = loffset & 0x7FFFFFFF;

    poff += 1u << nps;
    pos += __umul24((uint32_t)(fentry * fiprime), prime);
      pos -= entry;
    pos += ((int)pos < 0 ? prime : 0);
    pos += ((int)pos < 0 ? prime : 0);
    pos -= (pos >= prime ? prime : 0);
    pos -= (pos >= prime ? prime : 0);

    pos += __umul24(lpoff, prime);

    const uint32_t step = __umul24(var, prime);
    uint4 vpos = {pos,
                  pos + step,
                  pos + step * 2,
                  pos + step * 3};

    if (var*4 >= 32) {
      uint32_t *s1 = &sieve[vpos.x >> 5];
      uint32_t *s2 = &sieve[vpos.y >> 5];
      uint32_t *s3 = &sieve[vpos.z >> 5];
      uint32_t *s4 = &sieve[vpos.w >> 5];
      uint32_t *se = &sieve[SIZE];
      uint32_t bit1 = orb << (vpos.x & 31);
      uint32_t bit2 = orb << (vpos.y & 31);
      uint32_t bit3 = orb << (vpos.z & 31);
      uint32_t bit4 = orb << (vpos.w & 31);
      const uint32_t add = var*4*prime >> 5;
      while (s4 < se) {
        atomicOr(s1, bit1);
        atomicOr(s2, bit2);
        atomicOr(s3, bit3);
        atomicOr(s4, bit4);
        s1 += add;
        s2 += add;
        s3 += add;
        s4 += add;
      }

      if (s1 < se)
        atomicOr(s1, bit1);
      if (s2 < se)
        atomicOr(s2, bit2);
      if (s3 < se)
        atomicOr(s3, bit3);
    } else {


    const uint32_t add = step * 4;
    while (vpos.w < sieveBits) {
      atomicOr(&sieve[vpos.x >> 5], orb << (vpos.x & 31));
      atomicOr(&sieve[vpos.y >> 5], orb << (vpos.y & 31));
      atomicOr(&sieve[vpos.z >> 5], orb << (vpos.z & 31));
      atomicOr(&sieve[vpos.w >> 5], orb << (vpos.w & 31));
      vpos.x += add;
      vpos.y += add;
      vpos.z += add;
      vpos.w += add;
    }

    if (vpos.x < sieveBits)
      atomicOr(&sieve[vpos.x >> 5], orb << (vpos.x & 31));
    if (vpos.y < sieveBits)
      atomicOr(&sieve[vpos.y >> 5], orb << (vpos.y & 31));
    if (vpos.z < sieveBits)
      atomicOr(&sieve[vpos.z >> 5], orb << (vpos.z & 31));
    }
  }
  
  const uint2 *pprimes = &primes[id];
  const uint32_t *poffset = &offset[id];
  
  uint32_t plifo[NLIFO];
  uint32_t fiplifo[NLIFO];
  uint32_t olifo[NLIFO];

  for(int i = 0; i < NLIFO; ++i){
    pprimes += LSIZE;
    poffset += LSIZE;
    
    const uint2 tmp = *pprimes;
    plifo[i] = tmp.x;
    fiplifo[i] = tmp.y;
    olifo[i] = *poffset;
  }
  
  uint32_t lpos = 0;
  
#pragma unroll
  for (uint32_t ip = 1; ip < SIEVERANGE3; ++ip) {
    const uint32_t prime = plifo[lpos];
    const float fiprime = __int_as_float(fiplifo[lpos]);
    uint32_t pos = olifo[lpos];
    
    pos += __umul24((uint32_t)(fentry * fiprime), prime);
      pos -= entry;
    pos += ((int)pos < 0 ? prime : 0);
    pos += ((int)pos < 0 ? prime : 0);
    pos -= (pos >= prime ? prime : 0);
    pos -= (pos >= prime ? prime : 0);
    
    uint32_t index = pos >> 5;
    
    if(ip < SIEVERANGE1){
      uint2 vpos = {pos,
                    pos + prime};
        
      const uint32_t add = 2*prime;

      while (vpos.y < sieveBits) {
        atomicOr(&sieve[vpos.x >> 5], 1u << (vpos.x & 31));
        atomicOr(&sieve[vpos.y >> 5], 1u << (vpos.y & 31));
        vpos.x += add;
        vpos.y += add;
      }
        
      if (vpos.x < sieveBits)
        atomicOr(&sieve[vpos.x >> 5], 1u << (vpos.x & 31));
    } else if (ip < SIEVERANGE2) {
      if(index < SIZE){
        atomicOr(&sieve[index], 1u << (pos & 31));
        pos += prime;
        index = pos >> 5;
        if(index < SIZE){
          atomicOr(&sieve[index], 1u << (pos & 31));
          pos += prime;
          index = pos >> 5;
          if(index < SIZE){
            atomicOr(&sieve[index], 1u << (pos & 31));
          }
        }
      }
    } else if(ip < SIEVERANGE3) {
      if(index < SIZE){
        atomicOr(&sieve[index], 1u << (pos & 31));
        pos += prime;
        index = pos >> 5;
        if(index < SIZE){
          atomicOr(&sieve[index], 1u << (pos & 31));
        }
      }
    } else {
      if(index < SIZE){
        atomicOr(&sieve[index], 1u << (pos & 31));
      }
    }
    
    if(ip+NLIFO < SCOUNT/LSIZE){
      pprimes += LSIZE;
      poffset += LSIZE;
      
      const uint2 tmp = *pprimes;
      plifo[lpos] = tmp.x;
      fiplifo[lpos] = tmp.y;
      olifo[lpos] = *poffset;
    }
    
    lpos++;
    lpos = lpos & (NLIFO - 1);
  }

#pragma unroll
  for (uint32_t ip = SIEVERANGE3; ip < SCOUNT/LSIZE; ++ip) {
    const uint32_t prime = plifo[lpos];
    const float fiprime = __int_as_float(fiplifo[lpos]);
    uint32_t pos = olifo[lpos];

    pos += __umul24((uint32_t)(fentry * fiprime), prime);
      pos -= entry;
    pos += ((int)pos < 0 ? prime : 0);
    pos += ((int)pos < 0 ? prime : 0);
    pos -= (pos >= prime ? prime : 0);
    pos -= (pos >= prime ? prime : 0);

    uint32_t index = pos >> 5;
    if(index < SIZE)
      atomicOr(&sieve[index], 1u << (pos & 31));

    if(ip+NLIFO < SCOUNT/LSIZE){
      pprimes += LSIZE;
      poffset += LSIZE;

      const uint2 tmp = *pprimes;
      plifo[lpos] = tmp.x;
      fiplifo[lpos] = tmp.y;
      olifo[lpos] = *poffset;
    }

    lpos++;
    lpos = lpos & (NLIFO - 1);
  }

  __syncthreads();
  uint32_t *gsieve = &gsieve_all[SIZE*(STRIPES/2*line + stripe)];
  for (uint32_t i = id; i < SIZE; i += LSIZE)
    gsieve[i] = sieve[i];
}


__device__ __forceinline__ void appendFoundWarp(bool is320,
                                                const fermat_t &info,
                                                uint32_t *fcount,
                                                fermat_t *found320,
                                                fermat_t *found352)
{
  const unsigned lane = threadIdx.x & 31;

  const unsigned mask320 = __ballot_sync(0xFFFFFFFFu, is320);
  if (mask320) {
    const unsigned leader320 = __ffs(mask320) - 1;
    unsigned base320 = 0;
    if (lane == leader320)
      base320 = atomicAdd(&fcount[0], __popc(mask320));
    base320 = __shfl_sync(mask320, base320, leader320);

    if (is320) {
      const unsigned laneMask = (1u << lane) - 1;
      const unsigned rank320 = __popc(mask320 & laneMask);
      found320[base320 + rank320] = info;
    }
  }

  const bool is352 = !is320;
  const unsigned mask352 = __ballot_sync(0xFFFFFFFFu, is352);
  if (mask352) {
    const unsigned leader352 = __ffs(mask352) - 1;
    unsigned base352 = 0;
    if (lane == leader352)
      base352 = atomicAdd(&fcount[1], __popc(mask352));
    base352 = __shfl_sync(mask352, base352, leader352);

    if (is352) {
      const unsigned laneMask = (1u << lane) - 1;
      const unsigned rank352 = __popc(mask352 & laneMask);
      found352[base352 + rank352] = info;
    }
  }
}

__global__ void s_sieve(const uint32_t *gsieve1,
                        const uint32_t* gsieve2,
                        fermat_t *found320,
                        fermat_t *found352,
                        uint32_t *fcount,
                        uint32_t hashid,
                        uint32_t hashSize,
                        uint32_t depth)
{
  const uint32_t id = blockIdx.x * blockDim.x + threadIdx.x;

  uint32_t tmp1[WIDTH];
#pragma unroll
  for (int i = 0; i < WIDTH; ++i)
    tmp1[i] = gsieve1[SIZE*STRIPES/2*i + id];

#pragma unroll
  for (int start = 0; start <= WIDTH-TARGET; ++start){
    uint32_t mask = 0;

#pragma unroll
    for (int line = 0; line < TARGET; ++line)
      mask |= tmp1[start+line];

    if (mask != 0xFFFFFFFF) {
      unsigned bit = 31-__clz(~mask);
      unsigned multiplier = bit + id*32 + SIZE*32*STRIPES/2;  // mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-__clz(multiplier)) + start + depth;

      fermat_t info;
      info.index = multiplier;
      info.origin = start;
      info.chainpos = 0;
      info.type = 0;
      info.hashid = hashid;
      appendFoundWarp(maxSize <= 320, info, fcount, found320, found352);
    }
  }

  uint32_t tmp2[WIDTH];
#pragma unroll
  for (int i = 0; i < WIDTH; ++i)
    tmp2[i] = gsieve2[SIZE*STRIPES/2*i + id];

#pragma unroll
  for (int start = 0; start <= WIDTH-TARGET; ++start){
    uint32_t mask = 0;
#pragma unroll
    for (int line = 0; line < TARGET; ++line)
      mask |= tmp2[start+line];

    if (mask != 0xFFFFFFFF) {
      unsigned bit = 31-__clz(~mask);
      unsigned multiplier = bit + id*32 + SIZE*32*STRIPES/2;  // mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-__clz(multiplier)) + start + depth;

      fermat_t info;
      info.index = multiplier;
      info.origin = start;
      info.chainpos = 0;
      info.type = 1;
      info.hashid = hashid;
      appendFoundWarp(maxSize <= 320, info, fcount, found320, found352);
    }
  }

  const unsigned bitwinLayers = (TARGET / 2) + (TARGET % 2);
#pragma unroll
  for (int i = 0; i < WIDTH; ++i)
    tmp2[i] |= tmp1[i];
#pragma unroll
  for (int start = 0; start <= WIDTH-bitwinLayers; ++start) {
    uint32_t mask = 0;
#pragma unroll
    for(int line = 0; line < TARGET/2; ++line)
      mask |= tmp2[start+line];

    if(TARGET & 1u)
      mask |= tmp1[start+TARGET/2];

    if (mask != 0xFFFFFFFF) {
      unsigned bit = 31-__clz(~mask);
      unsigned multiplier = bit + id*32 + SIZE*32*STRIPES/2;  // mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-__clz(multiplier)) + start + (depth/2) + (depth&1);

      fermat_t info;
      info.index = multiplier;
      info.origin = start;
      info.chainpos = 0;
      info.type = 2;
      info.hashid = hashid;
      appendFoundWarp(maxSize <= 320, info, fcount, found320, found352);
    }
  }
}
