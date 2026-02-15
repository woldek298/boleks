#include "zcashgpuclient.h"
#include "equihash_original.h"
#include "../sha256.h"
#include "../base58.h"
extern "C" {
#include "../adl.h"
}

#include <gmpxx.h>
cl_platform_id gPlatform = 0;

namespace silentarmy {
typedef struct  blake2b_state_s 
{
    uint64_t    h[8];
    uint64_t    bytes;
}               blake2b_state_t; 


static const uint32_t   blake2b_block_len = 128;
static const uint32_t   blake2b_rounds = 12;
static const uint64_t   blake2b_iv[8] =
{
    0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
    0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
    0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
    0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
};
static const uint8_t    blake2b_sigma[12][16] =
{
      {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
      { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
      { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
      {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
      {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
      {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
      { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
      { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
      {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
      { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
      {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
      { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
};

/*
** Init the state according to Zcash parameters.
*/
void zcash_blake2b_init(blake2b_state_t *st, uint8_t hash_len,
  uint32_t n, uint32_t k)
{
    assert(n > k);
    assert(hash_len <= 64);
    st->h[0] = blake2b_iv[0] ^ (0x01010000 | hash_len);
    for (uint32_t i = 1; i <= 5; i++)
        st->h[i] = blake2b_iv[i];
    st->h[6] = blake2b_iv[6] ^ *(uint64_t *)"ZcashPoW";
    st->h[7] = blake2b_iv[7] ^ (((uint64_t)k << 32) | n);
    st->bytes = 0;
}

static uint64_t rotr64(uint64_t a, uint8_t bits)
{
    return (a >> bits) | (a << (64 - bits));
}

static void mix(uint64_t *va, uint64_t *vb, uint64_t *vc, uint64_t *vd,
        uint64_t x, uint64_t y)
{
    *va = (*va + *vb + x);
    *vd = rotr64(*vd ^ *va, 32);
    *vc = (*vc + *vd);
    *vb = rotr64(*vb ^ *vc, 24);
    *va = (*va + *vb + y);
    *vd = rotr64(*vd ^ *va, 16);
    *vc = (*vc + *vd);
    *vb = rotr64(*vb ^ *vc, 63);
}

/*
** Process either a full message block or the final partial block.
** Note that v[13] is not XOR'd because st->bytes is assumed to never overflow.
**
** _msg         pointer to message (must be zero-padded to 128 bytes if final block)
** msg_len      must be 128 (<= 128 allowed only for final partial block)
** is_final     indicate if this is the final block
*/
void zcash_blake2b_update(blake2b_state_t *st, const uint8_t *_msg,
        uint32_t msg_len, uint32_t is_final)
{
    const uint64_t      *m = (const uint64_t *)_msg;
    uint64_t            v[16];
    assert(msg_len <= 128);
    assert(st->bytes <= UINT64_MAX - msg_len);
    memcpy(v + 0, st->h, 8 * sizeof (*v));
    memcpy(v + 8, blake2b_iv, 8 * sizeof (*v));
    v[12] ^= (st->bytes += msg_len);
    v[14] ^= is_final ? -1 : 0;
    for (uint32_t round = 0; round < blake2b_rounds; round++)
      {
        const uint8_t   *s = blake2b_sigma[round];
        mix(v + 0, v + 4, v + 8,  v + 12, m[s[0]],  m[s[1]]);
        mix(v + 1, v + 5, v + 9,  v + 13, m[s[2]],  m[s[3]]);
        mix(v + 2, v + 6, v + 10, v + 14, m[s[4]],  m[s[5]]);
        mix(v + 3, v + 7, v + 11, v + 15, m[s[6]],  m[s[7]]);
        mix(v + 0, v + 5, v + 10, v + 15, m[s[8]],  m[s[9]]);
        mix(v + 1, v + 6, v + 11, v + 12, m[s[10]], m[s[11]]);
        mix(v + 2, v + 7, v + 8,  v + 13, m[s[12]], m[s[13]]);
        mix(v + 3, v + 4, v + 9,  v + 14, m[s[14]], m[s[15]]);
      }
    for (uint32_t i = 0; i < 8; i++)
        st->h[i] ^= v[i] ^ v[i + 8];
}

}

double GetPrimeDifficulty(unsigned int nBits)
{
  uint256 powLimit256("03ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
  uint32_t powLimit = powLimit256.GetCompact(false);
  int nShift = (nBits >> 24) & 0xff;
  int nShiftAmount = (powLimit >> 24) & 0xff;

  double dDiff = (double)(powLimit & 0x00ffffff) /  (double)(nBits & 0x00ffffff);
  while (nShift < nShiftAmount) {
    dDiff *= 256.0;
    nShift++;
  }
  
  while (nShift > nShiftAmount) {
    dDiff /= 256.0;
    nShift--;
  }

  return dDiff;
}

// static void setheader(crypto_generichash_blake2b_state *ctx, const char *header, const uint32_t headerlen)
// {
//   uint32_t le_N = WN;
//   uint32_t le_K = WK;
//   char personal[] = "ZcashPoW01230123";
//   memcpy(personal+8,  &le_N, 4);
//   memcpy(personal+12, &le_K, 4);
//   crypto_gen P[1];
//   P->digest_length = HASHOUT;
//   P->key_length    = 0;
//   P->fanout        = 1;
//   P->depth         = 1;
//   P->leaf_length   = 0;
//   P->node_offset   = 0;
//   P->node_depth    = 0;
//   P->inner_length  = 0;
//   memset(P->reserved, 0, sizeof(P->reserved));
//   memset(P->salt,     0, sizeof(P->salt));
//   memcpy(P->personal, (const uint8_t *)personal, 16);
//   blake2b_init_param(ctx, P);
//   blake2b_update(ctx, (const uint8_t*)header, headerlen);
// }
/*
static void setnonce(blake2b_state *ctx, const uint8_t *nonce)
{
  blake2b_update(ctx, nonce, 32);
}*/

unsigned writeCompactSize(size_t size, uint8_t *out)
{
  if (size < 253) {
    out[0] = size;
    return 1;
  } else if (size <= std::numeric_limits<unsigned short>::max()) {
    out[0] = 253;
    *(uint16_t*)(out+1) = size;
    return 3;
  } else if (size <= std::numeric_limits<unsigned int>::max()) {
    out[0] = 254;
    *(uint32_t*)(out+1) = size;
    return 5;
  } else {
    out[0] = 255;
    *(uint64_t*)(out+1) = size;
  }
  return 0;
}

BaseClient *createClient(void *ctx)
{
  return new ZCashGPUClient(ctx);
}

inline void mpz_set_uint256(mpz_t r, uint256& u)
{
    mpz_import(r, 32 / sizeof(unsigned long), -1, sizeof(unsigned long), -1, 0, &u);
}

static void hexdump(uint8_t *a, uint32_t a_len)
{
  for (uint32_t i = 0; i < a_len; i++)
    fprintf(stdout, "%02x", a[i]);
}


bool ZCashMiner::CheckEquihashSolution(const CBlockHeader *pblock, const uint8_t *proof, size_t size)
{
  crypto_generichash_blake2b_state state;
  Eh200_9.InitialiseState(state);
  crypto_generichash_blake2b_update(&state, (const uint8_t*)&pblock->data, sizeof(pblock->data));
  crypto_generichash_blake2b_update(&state, (const uint8_t*)pblock->nNonce.begin(), 32);
  std::vector<uint8_t> proofForCheck(proof, proof+size);
  return Eh200_9.IsValidSolution(state, proofForCheck);
}


static unsigned nr_compute_units(const char *gpu)
{
  if (!strcmp(gpu, "rx480"))
    return 36;
  fprintf(stderr, "Unknown GPU: %s\n", gpu);
    return 0;
}

static size_t select_work_size_blake(void)
{
  size_t work_size = 64 *        /* thread per wavefront */
                     BLAKE_WPS * /* wavefront per simd */
                     4 *         /* simd per compute unit */
                     nr_compute_units("rx480");
  
  // Make the work group size a multiple of the nr of wavefronts, while
  // dividing the number of inputs. This results in the worksize being a
  // power of 2.
  while (NR_INPUTS % work_size)
    work_size += 64;
  return work_size;
}


static void init_ht(cl_command_queue queue,
                    cl_kernel k_init_ht,
                    clBuffer<uint8_t> &buf_ht,
                    clBuffer<uint8_t> &rowCounters)
{
  size_t global_ws = NR_ROWS / ROWS_PER_UINT;
  size_t local_ws = 64;
  cl_int status;
  
  OCL(clSetKernelArg(k_init_ht, 0, sizeof (cl_mem), &buf_ht.DeviceData));
  OCL(clSetKernelArg(k_init_ht, 1, sizeof(cl_mem), &rowCounters.DeviceData));
  OCL(clEnqueueNDRangeKernel(queue, k_init_ht, 1, NULL, &global_ws, &local_ws, 0, NULL, NULL));
}

static void sort_pair(uint32_t *a, uint32_t len)
{
    uint32_t    *b = a + len;
    uint32_t     tmp, need_sorting = 0;
    for (uint32_t i = 0; i < len; i++)
  if (need_sorting || a[i] > b[i])
    {
      need_sorting = 1;
      tmp = a[i];
      a[i] = b[i];
      b[i] = tmp;
    }
  else if (a[i] < b[i])
      return ;
}

uint32_t verify_sol(sols_t *sols, unsigned sol_i)
{
    uint32_t  *inputs = sols->values[sol_i];
    uint32_t  seen_len = (1 << (PREFIX + 1))/8;
    std::unique_ptr<uint8_t[]> seen(new uint8_t[seen_len]);
    uint32_t  i;
    uint8_t tmp;
    // look for duplicate inputs
    memset(seen.get(), 0, seen_len);
    for (i = 0; i < (1 << PARAM_K); i++) {
      if ((inputs[i]/8) >= seen_len) {
        sols->valid[sol_i] = 0;
        return 0;
      }
        
      tmp = seen[inputs[i] / 8];
      seen[inputs[i] / 8] |= 1 << (inputs[i] & 7);
      if (tmp == seen[inputs[i] / 8]) {
        // at least one input value is a duplicate
        sols->valid[sol_i] = 0;
        return 0;
      }
    }
    
    // the valid flag is already set by the GPU, but set it again because
    // I plan to change the GPU code to not set it
    sols->valid[sol_i] = 1;
    // sort the pairs in place
    for (uint32_t level = 0; level < PARAM_K; level++)
  for (i = 0; i < (1 << PARAM_K); i += (2 << level))
      sort_pair(&inputs[i], 1 << level);
    return 1;
}

static void compress(uint8_t *out, uint32_t *inputs, uint32_t n)
{
    uint32_t byte_pos = 0;
    int32_t bits_left = PREFIX + 1;
    uint8_t x = 0;
    uint8_t x_bits_used = 0;
    uint8_t *pOut = out;
    while (byte_pos < n)
      {
        if (bits_left >= 8 - x_bits_used)
          {
            x |= inputs[byte_pos] >> (bits_left - 8 + x_bits_used);
            bits_left -= 8 - x_bits_used;
            x_bits_used = 8;
          }
        else if (bits_left > 0)
          {
            uint32_t mask = ~(-1 << (8 - x_bits_used));
            mask = ((~mask) >> bits_left) & mask;
            x |= (inputs[byte_pos] << (8 - x_bits_used - bits_left)) & mask;
            x_bits_used += bits_left;
            bits_left = 0;
          }
        else if (bits_left <= 0)
          {
            assert(!bits_left);
            byte_pos++;
            bits_left = PREFIX + 1;
          }
        if (x_bits_used == 8)
          {
            *pOut++ = x;
            x = x_bits_used = 0;
          }
      }
}


ZCashMiner::ZCashMiner(unsigned id) : mID(id) {}


bool MinerInstance::init(cl_context context,
                         cl_program program, 
                         cl_device_id dev,
                         unsigned int threadsNum,
                         unsigned int threadsPerBlock)
{
  cl_int error;
  
  _context = context;
  _program = program;
  queue = clCreateCommandQueue(context, dev, 0, &error);
  
#ifdef ENABLE_DEBUG
    size_t              dbg_size = NR_ROWS;
#else
    size_t              dbg_size = 1;
#endif  

  buf_dbg.init(context, dbg_size, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS);
  for (unsigned i = 0; i < PARAM_K; i++)
    ht[i].init(context, HT_SIZE, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS);
  rowCounters1.init(context, NR_ROWS*2, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS);
  rowCounters2.init(context, NR_ROWS*2, CL_MEM_READ_WRITE | CL_MEM_HOST_NO_ACCESS);  
  buf_potential_sols.init(context, 1, CL_MEM_READ_WRITE);
  buf_sols.init(context, 1, CL_MEM_READ_WRITE);  
  fprintf(stderr, "Hash tables will use %.1f MB\n", 2.0 * HT_SIZE / 1e6);
  
  k_init_ht = clCreateKernel(program, "kernel_init_ht", &error);
  for (unsigned i = 0; i < WK; i++) {
    char kernelName[128];
    sprintf(kernelName, "kernel_round%d", i);
    k_rounds[i] = clCreateKernel(program, kernelName, &error);
  }
  
  k_potential_sols = clCreateKernel(program, "kernel_potential_sols", &error);
  k_sols = clCreateKernel(program, "kernel_sols", &error);
  return true;
}


bool ZCashMiner::Initialize(cl_context context,
                            cl_program program,
                            cl_device_id dev,
                            unsigned pipelines,
                            unsigned threadsNum,
                            unsigned threadsPerBlock)
{
  pipelinesNum = pipelines;
  miners = new MinerInstance[pipelines];
  for (unsigned i = 0; i < pipelines; i++)
    miners[i].init(context, program, dev, threadsNum, threadsPerBlock);
  _threadsNum = threadsNum;
  _threadsPerBlocksNum = threadsPerBlock;
  printf("threads: %u, work size: %u\n", threadsNum, threadsPerBlock);
  return true;
}

void ZCashMiner::InvokeMining(void *args, void *ctx, void *pipe) {
  
  ((ZCashMiner*)args)->Mining(ctx, pipe); 
}

void ZCashMiner::Mining(void *ctx, void *pipe)
{
  void* blocksub = zmq_socket(ctx, ZMQ_SUB);
  void* worksub = zmq_socket(ctx, ZMQ_SUB);
  void* statspush = zmq_socket(ctx, ZMQ_PUSH);
  void* sharepush = zmq_socket(ctx, ZMQ_PUSH); 
  
  zmq_connect(blocksub, "inproc://blocks");
  zmq_connect(worksub, "inproc://work");
  zmq_connect(statspush, "inproc://stats");
  zmq_connect(sharepush, "inproc://shares"); 
  
  {
    const char one[2] = {1, 0};
    zmq_setsockopt (blocksub, ZMQ_SUBSCRIBE, one, 1);
    zmq_setsockopt (worksub, ZMQ_SUBSCRIBE, one, 1);
  }
  
  proto::Block block;
  proto::Work work;
  proto::Share share;
  
  block.set_height(1);
  work.set_height(0);
  
  share.set_addr(gAddr);
  share.set_name(gClientName);
  share.set_clientid(gClientID);

  stats_t stats;
  stats.id = mID;
  stats.sols = 0;
  
  czmq_signal(pipe);
  czmq_poll(pipe, -1); 
  
  uint8_t compactSizeData[16];
  unsigned compactSize = writeCompactSize(COMPRESSED_PROOFSIZE, compactSizeData);
  
  CBlockHeader header;
  uint256 hashTarget;
  uint256 shareTarget;
  SHA_256 sha;  
  bool run = true;
  time_t statsTimeLabel = time(0);
  
  int currentInstance = 0;
  int readyInstance = -(int)pipelinesNum;
  
  printf("ZCash GPU miner thread %d started\n", mID);
  while(run){
    auto timeDiff = time(0) - statsTimeLabel;
    if (timeDiff >= 10) {
      stats.sols = calcStats();
      zmq_send(statspush, &stats, sizeof(stats), 0);
      statsTimeLabel = 0;
      cleanupStats();
    }
    
    if(czmq_poll(pipe, 0)){
      czmq_wait(pipe);
      czmq_wait(pipe);
    }
    
    // get work
    bool reset = false;
    {
      bool getwork = true;
      while(getwork && run){
        
        if(czmq_poll(worksub, 0) || work.height() < block.height()){
          run = ReceivePub(work, worksub);
          reset = true;
        }
        
        getwork = false;
        if(czmq_poll(blocksub, 0) || work.height() > block.height()){
          run = ReceivePub(block, blocksub);
          getwork = true;
        }
      }
    }
    if(!run)
      break;
    
    // reset if new work
    if(reset) {
      header.data.nVersion = ZCashMiner::CBlockHeader::CURRENT_VERSION;
      header.data.hashPrevBlock.SetHex(block.hash());
      header.data.hashMerkleRoot.SetHex(work.merkle());
      header.data.hashReserved.SetHex(work.hashreserved());
      header.data.nTime = work.time();
      header.data.nBits = work.bits();
      header.nNonce = mID;
      header.nNonce <<= 64;
      
      {
        // setCompact
        hashTarget = work.bits() & 0x007FFFFF;
        unsigned exponent = work.bits() >> 24;
        if (exponent <= 3)
          hashTarget >>= 8*(3-exponent);
        else
          hashTarget <<= 8*(exponent-3);
      }
      
      {
        // setCompact
        shareTarget = block.reqdiff() & 0x007FFFFF;
        unsigned exponent = block.reqdiff() >> 24;
        if (exponent <= 3)
          shareTarget >>= 8*(3-exponent);
        else
          shareTarget <<= 8*(exponent-3);
      }

      sha.init();
      sha.update((unsigned char*)&header.data, sizeof(header.data));
      
      currentInstance = 0;
      readyInstance = -(int)pipelinesNum;      
    }
    
    MinerInstance &miner = miners[currentInstance];
    clFlush(miner.queue);
    
    silentarmy::blake2b_state_t initialCtx;
    silentarmy::zcash_blake2b_init(&initialCtx, ZCASH_HASH_LEN, PARAM_N, PARAM_K);
    silentarmy::zcash_blake2b_update(&initialCtx, (const uint8_t*)&header, 128, 0);

    miner.nonce = header.nNonce;
    size_t global_ws;
    size_t local_blake_work_size = BLAKE_WORKGROUP_SIZE;
    size_t local_round_work_size = ROUND_WORKGROUP_SIZE;
    size_t local_sols_work_size = SOLS_WORKGROUP_SIZE;
    for (unsigned round = 0; round < PARAM_K; round++) {
      bool evenRound = (round%2 == 0);
      clBuffer<uint8_t> &rowCountersFirst = evenRound ? miner.rowCounters1 : miner.rowCounters2;
      clBuffer<uint8_t> &rowCountersSecond = evenRound ? miner.rowCounters2 : miner.rowCounters1;

      init_ht(miner.queue, miner.k_init_ht, miner.ht[round], rowCountersFirst);

      if (round == 0) {
        OCL(clSetKernelArg(miner.k_rounds[round], 0, sizeof(cl_mem), &miner.ht[round].DeviceData));
        OCL(clSetKernelArg(miner.k_rounds[round], 1, sizeof(cl_mem), &rowCountersFirst.DeviceData));
        OCL(clSetKernelArg(miner.k_rounds[round], 3, sizeof(cl_ulong), &initialCtx.h[0]));
        OCL(clSetKernelArg(miner.k_rounds[round], 4, sizeof(cl_ulong), &initialCtx.h[1]));
        OCL(clSetKernelArg(miner.k_rounds[round], 5, sizeof(cl_ulong), &initialCtx.h[2]));
        OCL(clSetKernelArg(miner.k_rounds[round], 6, sizeof(cl_ulong), &initialCtx.h[3]));
        OCL(clSetKernelArg(miner.k_rounds[round], 7, sizeof(cl_ulong), &initialCtx.h[4]));
        OCL(clSetKernelArg(miner.k_rounds[round], 8, sizeof(cl_ulong), &initialCtx.h[5]));
        OCL(clSetKernelArg(miner.k_rounds[round], 9, sizeof(cl_ulong), &initialCtx.h[6]));
        OCL(clSetKernelArg(miner.k_rounds[round], 10, sizeof(cl_ulong), &initialCtx.h[7]));
        global_ws = select_work_size_blake();
      } else {
        OCL(clSetKernelArg(miner.k_rounds[round], 0, sizeof(cl_mem), &miner.ht[round-1].DeviceData));
        OCL(clSetKernelArg(miner.k_rounds[round], 1, sizeof(cl_mem), &miner.ht[round].DeviceData));
        OCL(clSetKernelArg(miner.k_rounds[round], 2, sizeof(cl_mem), &rowCountersSecond.DeviceData));
        OCL(clSetKernelArg(miner.k_rounds[round], 3, sizeof(cl_mem), &rowCountersFirst.DeviceData));        
        global_ws = NR_ROWS*ROUND_WORKGROUP_SIZE;
      }

      OCL(clSetKernelArg(miner.k_rounds[round], round == 0 ? 2 : 4, sizeof(cl_mem), &miner.buf_dbg.DeviceData));
      if (round == PARAM_K - 1) {
        OCL(clSetKernelArg(miner.k_rounds[round], 5, sizeof(cl_mem), &miner.buf_potential_sols.DeviceData));
        OCL(clSetKernelArg(miner.k_rounds[round], 6, sizeof(cl_mem), &miner.buf_sols.DeviceData));
      }
      OCL(clEnqueueNDRangeKernel(miner.queue, miner.k_rounds[round], 1, NULL, &global_ws, round == 0 ? &local_blake_work_size : &local_round_work_size, 0, NULL, NULL));
    }
    
    OCL(clSetKernelArg(miner.k_potential_sols, 0, sizeof(cl_mem), &miner.ht[8].DeviceData));
    OCL(clSetKernelArg(miner.k_potential_sols, 1, sizeof(cl_mem), &miner.rowCounters1.DeviceData));     
    OCL(clSetKernelArg(miner.k_potential_sols, 2, sizeof(cl_mem), &miner.buf_potential_sols.DeviceData));  
    global_ws = NR_ROWS*ROUND_WORKGROUP_SIZE;
    OCL(clEnqueueNDRangeKernel(miner.queue, miner.k_potential_sols, 1, NULL, &global_ws, &local_round_work_size, 0, NULL, NULL));    
    
    OCL(clSetKernelArg(miner.k_sols, 0, sizeof(cl_mem), &miner.ht[0].DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 1, sizeof(cl_mem), &miner.ht[1].DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 2, sizeof(cl_mem), &miner.ht[2].DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 3, sizeof(cl_mem), &miner.ht[3].DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 4, sizeof(cl_mem), &miner.ht[4].DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 5, sizeof(cl_mem), &miner.ht[5].DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 6, sizeof(cl_mem), &miner.ht[6].DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 7, sizeof(cl_mem), &miner.ht[7].DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 8, sizeof(cl_mem), &miner.ht[8].DeviceData));    
    OCL(clSetKernelArg(miner.k_sols, 9, sizeof(cl_mem), &miner.buf_sols.DeviceData));
    OCL(clSetKernelArg(miner.k_sols, 10, sizeof(cl_mem), &miner.buf_potential_sols.DeviceData));  
    global_ws = MAX_POTENTIAL_SOLS * SOLS_WORKGROUP_SIZE;
    OCL(clEnqueueNDRangeKernel(miner.queue, miner.k_sols, 1, NULL, &global_ws, &local_sols_work_size, 0, NULL, NULL));    
    
    if (readyInstance >= 0) {
      uint32_t nsols = 0;
      MinerInstance &readyMiner = miners[readyInstance];
      miner.buf_sols.copyToHost(miner.queue, true);
      sols_t *sols = miner.buf_sols.HostData;    
      if (sols->nr > MAX_SOLS)
        sols->nr = MAX_SOLS;
    
      for (unsigned sol_i = 0; sol_i < sols->nr; sol_i++)
        verify_sol(sols, sol_i);

      uint8_t proof[COMPRESSED_PROOFSIZE*2];
      for (uint32_t i = 0; i < sols->nr; i++) {
        if (sols->valid[i]) {
          compress(proof, (uint32_t *)(sols->values[i]), 1 << PARAM_K);
          nsols++;
    
          // calculate hash and send share if need
          bool isShare = false;
          bool isBlock = false;
          uint256 headerHash;
      

          SHA_256 current_sha = sha;
          current_sha.update(miner.nonce.begin(), miner.nonce.size());        
          current_sha.update(compactSizeData, compactSize);
          current_sha.update(proof, COMPRESSED_PROOFSIZE);
          current_sha.final((unsigned char*)&headerHash);
          current_sha.init();
          current_sha.update((unsigned char*)&headerHash, sizeof(uint256));
          current_sha.final((unsigned char*)&headerHash);
      
          if (headerHash <= hashTarget) {
            // block found
            isShare = true;
            isBlock = true;
            printf("GPU %u found a BLOCK! hash=%s\n", mID, headerHash.ToString().c_str());
          } else if (headerHash <= shareTarget) {
            // share found
            isShare = true;
            printf("GPU %d found share\n", mID);
          }
                  
          if (isShare) {
            // check equihash
            bool isValid = CheckEquihashSolution(&header, proof, COMPRESSED_PROOFSIZE);
            if (!isValid) {
              printf(" * GPU %d found invalid share\n", mID);
              continue;
            }
            
            share.set_hash(headerHash.GetHex());
            share.set_merkle(work.merkle()); 
            share.set_time(header.data.nTime);
            share.set_bits(work.bits());
            share.set_nonce(0);
            share.set_multi("");
            share.set_height(block.height());
            share.set_length(0);
            share.set_chaintype(0);
            share.set_isblock(isBlock);
            share.set_bignonce(readyMiner.nonce.GetHex());
            share.set_proofofwork(proof, COMPRESSED_PROOFSIZE);
            Send(share, sharepush);
          }      
        }
      }
      
      pushStats(nsols);      
    }
    
    ++header.nNonce;
    currentInstance = (currentInstance+1) % pipelinesNum;
    readyInstance = (readyInstance+1);
    if (readyInstance >= 0)
      readyInstance %= pipelinesNum;
  }
  
  printf("thread %d stopped.\n", mID); 
  zmq_close(blocksub);
  zmq_close(worksub);
  zmq_close(statspush);
  zmq_close(sharepush);
  
  czmq_signal(pipe);    
}

ZCashGPUClient::~ZCashGPUClient()
{
}

int ZCashGPUClient::GetStats(proto::ClientStats& stats)
{
  unsigned nw = mWorkers.size();
  std::vector<bool> running(nw);
  std::vector<stats_t> wstats(nw);
  
  while(czmq_poll(mStatsPull, 0)) {
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_recvmsg(mStatsPull, &msg, 0);          
    size_t fsize = zmq_msg_size(&msg);
    uint8_t *fbytes = (uint8_t*)zmq_msg_data(&msg);
    if (fsize >= sizeof(stats_t)) {
      stats_t *tmp = (stats_t*)fbytes;
      if(tmp->id < nw) {
        running[tmp->id] = true;
        wstats[tmp->id] = *tmp;
      }
    }
    
    zmq_msg_close(&msg);
  }
  
  double sols = 0;
  int maxtemp = 0;
  unsigned ngpus = 0;
  int crashed = 0;
  
  for(unsigned i = 0; i < nw; ++i) {
    int devid = mDeviceMapRev[i];
    int temp = gpu_temp(devid);
    int activity = gpu_activity(devid);
    
    if(temp > maxtemp)
      maxtemp = temp;
    
    sols += wstats[i].sols;
    
    if(running[i]) {
      ngpus++;
      printf("[GPU %d] T=%dC A=%d%% sols=%.3lf\n", i, temp, activity, wstats[i].sols);
    } else if (!mWorkers[i].first)
      printf("[GPU %d] failed to start!\n", i);
    else if(mPaused) {
      printf("[GPU %d] paused\n", i);
    } else {
      crashed++;
      printf("[GPU %d] crashed!\n", i);
    }
  }
  
//   if (crashed) {
//     if (exitType == 1) {
//       exit(1);
//     } else if (exitType == 2) {
// #ifdef WIN32
//       ExitWindowsEx(EWX_REBOOT, 0);
// #else
//       system("/sbin/reboot");
// #endif
//     }
//   }
  
  if(mStatCounter % 10 == 0)
    for(unsigned i = 0; i < mNumDevices; ++i){
      int gpuid = mDeviceMap[i];
      if(gpuid >= 0)
        printf("GPU %d: core=%dMHz mem=%dMHz powertune=%d fanspeed=%d\n",
            gpuid, gpu_engineclock(i), gpu_memclock(i), gpu_powertune(i), gpu_fanspeed(i));
    }
  
  stats.set_cpd(sols);
  stats.set_errors(0);
  stats.set_temp(maxtemp);
  stats.set_unittype(1); // 0 for CPU, 1 for GPU
  
  mStatCounter++;
  
  return ngpus;  
}


bool ZCashGPUClient::Initialize(Configuration *cfg, bool benchmarkOnly, unsigned adjustedKernelTarget)
{
  cl_context gContext[64] = {0};
  cl_program gProgram[64] = {0};  
  
  const char *platformId = cfg->lookupString("", "platform");
  const char *platformName = "";
  if (strcmp(platformId, "amd") == 0) {
    platformName = "AMD Accelerated Parallel Processing";
    platformType = ptAMD;    
  } else if (strcmp(platformId, "nvidia") == 0) {
    platformName = "NVIDIA CUDA";
    platformType = ptNVidia;    
  }
  
  int enableOverclocking = cfg->lookupInt("", "enable_overclocking", 0);
  if (platformType == ptAMD && enableOverclocking)
    setup_adl();    
  
  std::vector<cl_device_id> allGpus;
  if (!clInitialize(platformName, allGpus)) {
    return false;
  }
    
  unsigned instancesNum = 1;    
  mNumDevices = allGpus.size();
  std::vector<bool> usegpu(mNumDevices, true);
  std::vector<int> threads(mNumDevices, 8192);
  std::vector<int> worksize(mNumDevices, 128);  
  mCoreFreq = std::vector<int>(mNumDevices, -1);
  mMemFreq = std::vector<int>(mNumDevices, -1);
  mPowertune = std::vector<int>(mNumDevices, 42);
  mFanSpeed = std::vector<int>(mNumDevices, 70);
  
  {
    const char *address = cfg->lookupString("", "address");    
    CBitcoinAddress btcAddress(address);
    if (!btcAddress.IsValidForZCash()) {
      printf("This address is not valid ZCash T-address: %s\n", address);
      exit(1);
    }
    
    StringVector cworksizes;
    StringVector cthreads;
    StringVector cdevices;
    StringVector ccorespeed;
    StringVector cmemspeed;
    StringVector cpowertune;
    StringVector cfanspeed;
    
    try {
      cfg->lookupList("", "worksizes", cworksizes);
      cfg->lookupList("", "threads", cthreads);      
      cfg->lookupList("", "devices", cdevices);
      cfg->lookupList("", "corefreq", ccorespeed);
      cfg->lookupList("", "memfreq", cmemspeed);
      cfg->lookupList("", "powertune", cpowertune);
      cfg->lookupList("", "fanspeed", cfanspeed);
      instancesNum = cfg->lookupInt("", "instances", 1);
    } catch(const ConfigurationException& ex) {}
    
    for(int i = 0; i < (int)mNumDevices; ++i) {
      if(i < cdevices.length())
        usegpu[i] = !strcmp(cdevices[i], "1");     
      if(i < cworksizes.length())
        worksize[i] = atoi(cworksizes[i]);
      if(i < cthreads.length())
        threads[i] = atoi(cthreads[i]);      
      if(i < ccorespeed.length())
        mCoreFreq[i] = atoi(ccorespeed[i]);
      if(i < cmemspeed.length())
        mMemFreq[i] = atoi(cmemspeed[i]);
      if(i < cpowertune.length())
        mPowertune[i] = atoi(cpowertune[i]);
      if(i < cfanspeed.length())
        mFanSpeed[i] = atoi(cfanspeed[i]);                        
    }
  }  
  
  std::vector<cl_device_id> gpus;
  for(unsigned i = 0; i < mNumDevices; ++i) {
    if (usegpu[i]) {
      printf("Using device %d as GPU %d\n", i, (int)gpus.size());
      mDeviceMap[i] = gpus.size();
      mDeviceMapRev[gpus.size()] = i;
      gpus.push_back(allGpus[i]);
    }else{
      mDeviceMap[i] = -1;
    }
  }
  
  if(!gpus.size()){
    printf("EXIT: config.txt says not to use any devices!?\n");
    return false;
  }
  
  // context create
  for (unsigned i = 0; i < gpus.size(); i++) {
    cl_context_properties props[] = { CL_CONTEXT_PLATFORM, (cl_context_properties)gPlatform, 0 };
    cl_int error;
    gContext[i] = clCreateContext(props, 1, &gpus[i], 0, 0, &error);
    OCLR(error, false);
  }
  
  std::vector<cl_int> binstatus;
  binstatus.resize(gpus.size());
  
  for (size_t i = 0; i < gpus.size(); i++) {
    char kernelName[64];
    sprintf(kernelName, "silentarmy_gpu%u.bin", (unsigned)i);
    if (!clCompileKernel(gContext[i],
                         gpus[i],
                         kernelName,
                         { "zcash/gpu/param.h", "zcash/gpu/kernel.cl" },
                         "",
                         &binstatus[i],
                         &gProgram[i],
                         false))
      return false;
  }

  for(unsigned i = 0; i < gpus.size()*instancesNum; ++i) {
      std::pair<ZCashMiner*,void*> worker;
      if (binstatus[i/instancesNum] == CL_SUCCESS) {
      
        ZCashMiner *miner = new ZCashMiner(i);
        if (!miner->Initialize(gContext[i/instancesNum], gProgram[i/instancesNum], gpus[i/instancesNum], 1, threads[i/instancesNum], worksize[i/instancesNum]))
          return false;
        
        void *pipe = czmq_thread_fork(mCtx, &ZCashMiner::InvokeMining, miner);
        czmq_wait(pipe);
        czmq_signal(pipe);
        worker.first = miner;
        worker.second = pipe;
      } else {
        printf("GPU %d: failed to load kernel\n", i);
        worker.first = 0;
        worker.second = 0;
      
      }
    
      mWorkers.push_back(worker);
    }  

  return true;
}

void ZCashGPUClient::NotifyBlock(const proto::Block& block)
{
  SendPub(block, mBlockPub); 
}

bool ZCashGPUClient::TakeWork(const proto::Work& work)
{
  SendPub(work, mWorkPub);
	return true;
}

void ZCashGPUClient::Toggle()
{
  for(unsigned i = 0; i < mWorkers.size(); ++i)
    if(mWorkers[i].first){
      czmq_signal(mWorkers[i].second);
    }
  
  mPaused = !mPaused;
}


void ZCashGPUClient::setup_adl(){
  
  init_adl(mNumDevices);
  
  for(unsigned i = 0; i < mNumDevices; ++i){
    
    if(mCoreFreq[i] > 0)
      if(set_engineclock(i, mCoreFreq[i]))
        printf("set_engineclock(%d, %d) failed.\n", i, mCoreFreq[i]);
    if(mMemFreq[i] > 0)
      if(set_memoryclock(i, mMemFreq[i]))
        printf("set_memoryclock(%d, %d) failed.\n", i, mMemFreq[i]);
    if(mPowertune[i] >= -20 && mPowertune[i] <= 20)
      if(set_powertune(i, mPowertune[i]))
        printf("set_powertune(%d, %d) failed.\n", i, mPowertune[i]);
    if (mFanSpeed[i] > 0)
      if(set_fanspeed(i, mFanSpeed[i]))
        printf("set_fanspeed(%d, %d) failed.\n", i, mFanSpeed[i]);
  }
}
