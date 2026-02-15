#define PARAM_N       200
#define PARAM_K       9
#define PREFIX                          (PARAM_N / (PARAM_K + 1))
#define NR_INPUTS                       (1 << PREFIX)
// Approximate log base 2 of number of elements in hash tables
#define APX_NR_ELMS_LOG                 (PREFIX + 1)
// Number of rows and slots is affected by this. 20 offers the best performance
// but occasionally misses ~1% of solutions.
#define NR_ROWS_LOG                     12

// Number of collision items to track, per thread
#define BLAKE_WORKGROUP_SIZE 256
#define ROUND_WORKGROUP_SIZE 256
#define SOLS_WORKGROUP_SIZE 32

// Ratio of time of sleeping before rechecking if task is done (0-1)
#define SLEEP_RECHECK_RATIO 0.60
// Ratio of time to busy wait for the solution (0-1)
// The higher value the higher CPU usage with Nvidia
#define SLEEP_SKIP_RATIO 0.005

// Make hash tables OVERHEAD times larger than necessary to store the average
// number of elements per row. The ideal value is as small as possible to
// reduce memory usage, but not too small or else elements are dropped from the
// hash tables.
//
// The actual number of elements per row is closer to the theoretical average
// (less variance) when NR_ROWS_LOG is small. So accordingly OVERHEAD can be
// smaller.
//
// Even (as opposed to odd) values of OVERHEAD sometimes significantly decrease
// performance as they cause VRAM channel conflicts.
#if NR_ROWS_LOG == 11
#define COLLISION_PER_ROW               512u
#define COLLISION_BUFFER_SIZE           12u
#define NR_SLOTS 1184u
#elif NR_ROWS_LOG == 12
#define COLLISION_PER_ROW               256u
#define COLLISION_BUFFER_SIZE           16u
#define NR_SLOTS 608u
#elif NR_ROWS_LOG == 13
#define COLLISION_PER_ROW               128u
#define COLLISION_BUFFER_SIZE           16u
#define NR_SLOTS 336u
#elif NR_ROWS_LOG == 14
#define COLLISION_PER_ROW               64u
#define COLLISION_BUFFER_SIZE           14u
#define NR_SLOTS 176u
#elif NR_ROWS_LOG == 15
#define COLLISION_PER_ROW               32u
#define COLLISION_BUFFER_SIZE           12u
#define NR_SLOTS 112u
#elif NR_ROWS_LOG == 16
#define COLLISION_PER_ROW               16u
#define COLLISION_BUFFER_SIZE           12u
#define NR_SLOTS                        64u
#endif

#define NR_ROWS                         (1 << NR_ROWS_LOG)

// Length of 1 element (slot) in byte
#define SLOT_LEN_MAX                        32

#define SLOT_LEN(round)       ((UINTS_IN_XI(round) >= 6) ? 32 : \
                               (UINTS_IN_XI(round) >= 4) ? 24 : \
                               (UINTS_IN_XI(round) >= 2) ? 16 : \
                                                           8)

// Total size of hash table
#define HT_SIZE       (NR_ROWS * NR_SLOTS * SLOT_LEN_MAX)*2
// Length of Zcash block header, nonce (part of header)
#define ZCASH_BLOCK_HEADER_LEN    140
// Offset of nTime in header
#define ZCASH_BLOCK_OFFSET_NTIME        (4 + 3 * 32)
// Length of nonce
#define ZCASH_NONCE_LEN     32
// Length of encoded representation of solution size
#define ZCASH_SOLSIZE_LEN   3
// Solution size (1344 = 0x540) represented as a compact integer, in hex
#define ZCASH_SOLSIZE_HEX               "fd4005"
// Length of encoded solution (512 * 21 bits / 8 = 1344 bytes)
#define ZCASH_SOL_LEN                   ((1 << PARAM_K) * (PREFIX + 1) / 8)
// Last N_ZERO_BYTES of nonce must be zero due to my BLAKE2B optimization
#define N_ZERO_BYTES      12
// Number of bytes Zcash needs out of Blake
#define ZCASH_HASH_LEN                  50
// Number of wavefronts per SIMD for the Blake kernel.
// Blake is ALU-bound (beside the atomic counter being incremented) so we need
// at least 2 wavefronts per SIMD to hide the 2-clock latency of integer
// instructions. 10 is the max supported by the hw.
#define BLAKE_WPS                 10
// Maximum number of solutions reported by kernel to host
#define MAX_SOLS      10
#define MAX_POTENTIAL_SOLS       4096
// Length of SHA256 target
#define SHA256_TARGET_LEN               (256 / 8)

#if (NR_SLOTS < 16)
#define BITS_PER_ROW 4
#define ROWS_PER_UINT 8
#define ROW_MASK 0x0F
#elif (NR_SLOTS < 256)
#define BITS_PER_ROW 8
#define ROWS_PER_UINT 4
#define ROW_MASK 0xFF
#else
#define BITS_PER_ROW 16
#define ROWS_PER_UINT 2
#define ROW_MASK 0xFFFF
#endif

// Optional features
#undef ENABLE_DEBUG
#define NV_L2CACHE_OPT 5

/*
** Return the offset of Xi in bytes from the beginning of the slot.
*/

#define UINTS_IN_XI(round) (((round) == 0) ? 6 : \
                            ((round) == 1) ? 6 : \
                            ((round) == 2) ? 5 : \
                            ((round) == 3) ? 5 : \
                            ((round) == 4) ? 4 : \
                            ((round) == 5) ? 4 : \
                            ((round) == 6) ? 3 : \
                            ((round) == 7) ? 2 : \
                                             1)

// An (uncompressed) solution stores (1 << PARAM_K) 32-bit values
#define SOL_SIZE      ((1 << PARAM_K) * 4)
typedef struct  sols_s
{
    unsigned int nr;
    unsigned int  likely_invalids;
    unsigned char valid[MAX_SOLS];
    unsigned int  values[MAX_SOLS][(1 << PARAM_K)];
}   sols_t;

typedef struct  potential_sols_s
{
    unsigned int  nr;
    unsigned int  values[MAX_POTENTIAL_SOLS][2];
} potential_sols_t;
