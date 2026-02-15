#include "../baseclient.h"
#include "../uint256.h"
#include "../opencl.h"

typedef unsigned char uchar;
#include "gpu/param.h"

#include <list>

#define PROOFSIZE (1u<<WK)
#define COLLISION_BIT_LENGTH (WN / (WK+1))
#define COMPRESSED_PROOFSIZE ((COLLISION_BIT_LENGTH+1)*PROOFSIZE*4/(8*sizeof(uint32_t)))

typedef struct  debug_s
{
    uint32_t    dropped_coll;
    uint32_t    dropped_stor;
}               debug_t;

struct stats_t {
  unsigned id;
  double sols;
  
  stats_t(){
    id = 0;
    sols = 0;
  }
  
};

struct SHA_256;

struct MinerInstance {
  cl_context _context;
  cl_program _program;
  
  cl_command_queue queue;
  clBuffer<uint8_t> ht[PARAM_K];
  clBuffer<uint8_t> rowCounters1;
  clBuffer<uint8_t> rowCounters2;
  clBuffer<potential_sols_t> buf_potential_sols;
  clBuffer<sols_t> buf_sols;
  clBuffer<debug_t> buf_dbg;
  
  cl_kernel k_init_ht;
  cl_kernel k_rounds[PARAM_K];
  cl_kernel k_potential_sols;
  cl_kernel k_sols;
 
  uint256 nonce;
  
  MinerInstance() {}
  bool init(cl_context context, cl_program program, cl_device_id dev, unsigned threadsNum, unsigned threadsPerBlock);
};

class ZCashMiner {
private:
  struct solsPerSecond {
    time_t time;
    unsigned sols;
  };  
  
private:
  unsigned mID;
  unsigned pipelinesNum;
  MinerInstance *miners;
  unsigned _threadsNum;
  unsigned _threadsPerBlocksNum;  

  
  std::list<solsPerSecond> _stats;

  void pushStats(unsigned solsNum) {
    auto currentTime = time(0);
    if (_stats.empty()) {
      solsPerSecond s = {currentTime, solsNum};
      _stats.push_back(s);
    } else if (_stats.back().time == currentTime) {
      _stats.back().sols += solsNum;
    } else {
      solsPerSecond s = {currentTime, solsNum};
      _stats.push_back(s);
    }
  }
  
  void cleanupStats() {
    auto currentTime = time(0);
    while (!_stats.empty() &&_stats.front().time - currentTime >= 60)
      _stats.pop_front();
  }
  
  double calcStats() {
    auto currentTime = time(0);
    auto It = _stats.rbegin();
    auto last = currentTime;
    unsigned sum = 0;
    while (It != _stats.rend() && (It->time - currentTime) < 60) {
      last = It->time;
      sum += It->sols;
      ++It;
    }
    
    auto timeDiff = currentTime - last;
    return timeDiff ? (double)sum / timeDiff : 0;
  }
  

public:
#pragma pack(push, 1)
  struct CBlockHeader {
    // header
    static const size_t HEADER_SIZE=4+32+32+32+4+4+32; // excluding Equihash solution
    static const int32_t CURRENT_VERSION=4;
    
    struct {
      int32_t nVersion;
      uint256 hashPrevBlock;
      uint256 hashMerkleRoot;
      uint256 hashReserved;
      uint32_t nTime;
      uint32_t nBits;
    } data;
    
    uint256 nNonce;    
    
#pragma pack(pop)
    std::vector<unsigned char> nSolution;
  };
  
  bool _cancel;
  
public:
  ZCashMiner(unsigned id);
  
  bool CheckEquihashSolution(const CBlockHeader *pblock, const uint8_t *proof, size_t size);
  
  bool Initialize(cl_context context,
                  cl_program program,
                  cl_device_id dev,
                  unsigned pipelines,
                  unsigned threadsNum,
                  unsigned threadsPerBlock);
  static void InvokeMining(void *args, void *ctx, void *pipe);
  void Mining(void *ctx, void *pipe);
  void cancel() { _cancel = true; }
};

class ZCashGPUClient : public BaseClient {
public:
  ZCashGPUClient(void *ctx) : BaseClient(ctx) {};
  virtual ~ZCashGPUClient();
  
  bool Initialize(Configuration* cfg, bool benchmarkOnly, unsigned adjustedKernelTarget = 0);
  void NotifyBlock(const proto::Block& block);
  bool TakeWork(const proto::Work& work);
  int GetStats(proto::ClientStats& stats);
  void Toggle();
  void setup_adl();  
  
private:
  std::vector<std::pair<ZCashMiner*, void*> > mWorkers;
};
