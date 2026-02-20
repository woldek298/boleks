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

__device__ __forceinline__ uint32_t normalizePos(uint32_t baseOffset,
                                                 uint32_t prime,
                                                 uint32_t entry,
                                                 uint32_t reciprocal)
{
  uint32_t q = __umulhi(entry, reciprocal);
  uint32_t rem = entry - q * prime;
  rem -= (rem >= prime ? prime : 0);
  rem -= (rem >= prime ? prime : 0);

  uint32_t pos = baseOffset + prime - rem;
  pos -= (pos >= prime ? prime : 0);
  return pos;
}

__global__ void sieve(uint32_t *gsieve_all,
                      uint32_t* offset_all,
                      uint2 *primes)
{
  __shared__ uint32_t sieve[SIZE];
  
  const uint32_t id = threadIdx.x;
  const uint32_t stripe = blockIdx.x;
  const uint32_t line = blockIdx.y; 
  const uint32_t entry = SIZE*32*(stripe+STRIPES/2);
  
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
    const uint32_t reciprocal = tmp1.y;

    const uint32_t loffset = offset[poff+ip];
    const uint32_t orb = (loffset >> 31) ^ 0x1;
    uint32_t pos = loffset & 0x7FFFFFFF;

    poff += 1u << nps;
    if (!orb)
      continue;

    pos = normalizePos(pos, prime, entry, reciprocal);

    pos += lpoff * prime;

    uint4 vpos = {pos,
                  pos + var * prime,
                  pos + (var * 2u) * prime,
                  pos + (var * 3u) * prime};

    if (var*4 >= 32) {
      uint32_t *s1 = &sieve[vpos.x >> 5];
      uint32_t *s2 = &sieve[vpos.y >> 5];
      uint32_t *s3 = &sieve[vpos.z >> 5];
      uint32_t *s4 = &sieve[vpos.w >> 5];
      uint32_t *se = &sieve[SIZE];
      uint32_t bit1 = orb << (vpos.x % 32);
      uint32_t bit2 = orb << (vpos.y % 32);
      uint32_t bit3 = orb << (vpos.z % 32);
      uint32_t bit4 = orb << (vpos.w % 32);
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


    const uint32_t add = var*4*prime;
    while (vpos.w < SIZE*32) {
      atomicOr(&sieve[vpos.x >> 5], orb << (vpos.x%32));
      atomicOr(&sieve[vpos.y >> 5], orb << (vpos.y%32));
      atomicOr(&sieve[vpos.z >> 5], orb << (vpos.z%32));
      atomicOr(&sieve[vpos.w >> 5], orb << (vpos.w%32));
      vpos.x += add;
      vpos.y += add;
      vpos.z += add;
      vpos.w += add;
    }

    if (vpos.x < SIZE*32)
      atomicOr(&sieve[vpos.x >> 5], orb << (vpos.x%32));
    if (vpos.y < SIZE*32)
      atomicOr(&sieve[vpos.y >> 5], orb << (vpos.y%32));
    if (vpos.z < SIZE*32)
      atomicOr(&sieve[vpos.z >> 5], orb << (vpos.z%32));
    }
  }
  
  const uint2 *pprimes = &primes[id];
  const uint32_t *poffset = &offset[id];
  
  uint32_t plifo[NLIFO];
  uint32_t rlifo[NLIFO];
  uint32_t olifo[NLIFO];

  for(int i = 0; i < NLIFO; ++i){
    pprimes += LSIZE;
    poffset += LSIZE;
    
    const uint2 tmp = *pprimes;
    plifo[i] = tmp.x;
    rlifo[i] = tmp.y;
    olifo[i] = *poffset;
  }
  
  uint32_t lpos = 0;
  
#pragma unroll
  for (uint32_t ip = 1; ip < SIEVERANGE3; ++ip) {
    const uint32_t prime = plifo[lpos];
    const uint32_t reciprocal = rlifo[lpos];
    uint32_t pos = olifo[lpos];

    pos = normalizePos(pos, prime, entry, reciprocal);
    
    uint32_t index = pos >> 5;
    
    if(ip < SIEVERANGE1){
      uint2 vpos = {pos,
                    pos + prime};
        
      const uint32_t add = 2*prime;

      while (vpos.y < SIZE*32) {
        atomicOr(&sieve[vpos.x >> 5], 1u << (vpos.x%32));
        atomicOr(&sieve[vpos.y >> 5], 1u << (vpos.y%32));
        vpos.x += add;
        vpos.y += add;
      }
        
      if (vpos.x < SIZE*32)
        atomicOr(&sieve[vpos.x >> 5], 1u << (vpos.x % 32));
    } else if (ip < SIEVERANGE2) {
      if(index < SIZE){
        atomicOr(&sieve[index], 1u << (pos%32));
        pos += prime;
        index = pos >> 5;
        if(index < SIZE){
          atomicOr(&sieve[index], 1u << (pos%32));
          pos += prime;
          index = pos >> 5;
          if(index < SIZE){
            atomicOr(&sieve[index], 1u << (pos%32));
          }
        }
      }
    } else if(ip < SIEVERANGE3) {
      if(index < SIZE){
        atomicOr(&sieve[index], 1u << (pos%32));
        pos += prime;
        index = pos >> 5;
        if(index < SIZE){
          atomicOr(&sieve[index], 1u << (pos%32));
        }
      }
    } else {
      if(index < SIZE){
        atomicOr(&sieve[index], 1u << (pos%32));
      }
    }
    
    if(ip+NLIFO < SCOUNT/LSIZE){
      pprimes += LSIZE;
      poffset += LSIZE;
      
      const uint2 tmp = *pprimes;
      plifo[lpos] = tmp.x;
      rlifo[lpos] = tmp.y;
      olifo[lpos] = *poffset;
    }
    
    lpos++;
    lpos = lpos % NLIFO;
  }

#pragma unroll
  for (uint32_t ip = SIEVERANGE3; ip < SCOUNT/LSIZE; ++ip) {
    const uint32_t prime = plifo[lpos];
    const uint32_t reciprocal = rlifo[lpos];
    uint32_t pos = olifo[lpos];

    pos = normalizePos(pos, prime, entry, reciprocal);

    uint32_t index = pos >> 5;
    if(index < SIZE)
      atomicOr(&sieve[index], 1u << (pos%32));

    if(ip+NLIFO < SCOUNT/LSIZE){
      pprimes += LSIZE;
      poffset += LSIZE;

      const uint2 tmp = *pprimes;
      plifo[lpos] = tmp.x;
      rlifo[lpos] = tmp.y;
      olifo[lpos] = *poffset;
    }

    lpos++;
    lpos = lpos % NLIFO;
  }

  __syncthreads();
  uint32_t *gsieve = &gsieve_all[SIZE*(STRIPES/2*line + stripe)];
  for (uint32_t i = id; i < SIZE; i += LSIZE)
    gsieve[i] = sieve[i];
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
  const uint32_t lane = threadIdx.x & 31;
  const uint32_t activeMask = __activemask();
  const uint32_t laneMaskLt = (lane == 0) ? 0 : ((1u << lane) - 1);

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

    bool hit = mask != 0xFFFFFFFF;
    fermat_t info;
    bool is320 = false;
    if (hit) {
      unsigned bit = 31-__clz(~mask);
      unsigned multiplier = bit + id*32 + SIZE*32*STRIPES/2;  // mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-__clz(multiplier)) + start + depth;
      is320 = maxSize <= 320;

      info.index = multiplier;
      info.origin = start;
      info.chainpos = 0;
      info.type = 0;
      info.hashid = hashid;
    }

    const uint32_t mask320 = __ballot_sync(activeMask, hit && is320);
    if (mask320) {
      const uint32_t leader = __ffs(mask320) - 1;
      uint32_t base = 0;
      if (lane == leader)
        base = atomicAdd(&fcount[0], __popc(mask320));
      base = __shfl_sync(activeMask, base, leader);
      if (hit && is320) {
        const uint32_t rank = __popc(mask320 & laneMaskLt);
        found320[base + rank] = info;
      }
    }

    const uint32_t mask352 = __ballot_sync(activeMask, hit && !is320);
    if (mask352) {
      const uint32_t leader = __ffs(mask352) - 1;
      uint32_t base = 0;
      if (lane == leader)
        base = atomicAdd(&fcount[1], __popc(mask352));
      base = __shfl_sync(activeMask, base, leader);
      if (hit && !is320) {
        const uint32_t rank = __popc(mask352 & laneMaskLt);
        found352[base + rank] = info;
      }
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

    bool hit = mask != 0xFFFFFFFF;
    fermat_t info;
    bool is320 = false;
    if (hit) {
      unsigned bit = 31-__clz(~mask);
      unsigned multiplier = bit + id*32 + SIZE*32*STRIPES/2;  // mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-__clz(multiplier)) + start + depth;
      is320 = maxSize <= 320;

      info.index = multiplier;
      info.origin = start;
      info.chainpos = 0;
      info.type = 1;
      info.hashid = hashid;
    }

    const uint32_t mask320 = __ballot_sync(activeMask, hit && is320);
    if (mask320) {
      const uint32_t leader = __ffs(mask320) - 1;
      uint32_t base = 0;
      if (lane == leader)
        base = atomicAdd(&fcount[0], __popc(mask320));
      base = __shfl_sync(activeMask, base, leader);
      if (hit && is320) {
        const uint32_t rank = __popc(mask320 & laneMaskLt);
        found320[base + rank] = info;
      }
    }

    const uint32_t mask352 = __ballot_sync(activeMask, hit && !is320);
    if (mask352) {
      const uint32_t leader = __ffs(mask352) - 1;
      uint32_t base = 0;
      if (lane == leader)
        base = atomicAdd(&fcount[1], __popc(mask352));
      base = __shfl_sync(activeMask, base, leader);
      if (hit && !is320) {
        const uint32_t rank = __popc(mask352 & laneMaskLt);
        found352[base + rank] = info;
      }
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

    bool hit = mask != 0xFFFFFFFF;
    fermat_t info;
    bool is320 = false;
    if (hit) {
      unsigned bit = 31-__clz(~mask);
      unsigned multiplier = bit + id*32 + SIZE*32*STRIPES/2;  // mad24(id, 32u, (unsigned)bit) + SIZE*32*STRIPES/2;
      unsigned maxSize = hashSize + (32-__clz(multiplier)) + start + (depth/2) + (depth&1);
      is320 = maxSize <= 320;

      info.index = multiplier;
      info.origin = start;
      info.chainpos = 0;
      info.type = 2;
      info.hashid = hashid;
    }

    const uint32_t mask320 = __ballot_sync(activeMask, hit && is320);
    if (mask320) {
      const uint32_t leader = __ffs(mask320) - 1;
      uint32_t base = 0;
      if (lane == leader)
        base = atomicAdd(&fcount[0], __popc(mask320));
      base = __shfl_sync(activeMask, base, leader);
      if (hit && is320) {
        const uint32_t rank = __popc(mask320 & laneMaskLt);
        found320[base + rank] = info;
      }
    }

    const uint32_t mask352 = __ballot_sync(activeMask, hit && !is320);
    if (mask352) {
      const uint32_t leader = __ffs(mask352) - 1;
      uint32_t base = 0;
      if (lane == leader)
        base = atomicAdd(&fcount[1], __popc(mask352));
      base = __shfl_sync(activeMask, base, leader);
      if (hit && !is320) {
        const uint32_t rank = __popc(mask352 & laneMaskLt);
        found352[base + rank] = info;
      }
    }
  }
}
