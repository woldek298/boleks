#include "codegen/gcn.h"
#include <algorithm>

static inline const char *add_co_u32(GCNArchTy arch)
{
  return arch == gcn14 ? "v_add_co_u32" : "v_add_u32";
}

static inline const char *addc_co_u32(GCNArchTy arch)
{
  return arch == gcn14 ? "v_addc_co_u32" : "v_addc_u32";
}

static inline const char *sub_co_u32(GCNArchTy arch)
{
  return arch == gcn14 ? "v_sub_co_u32" : "v_sub_u32";
}

static inline const char *subb_co_u32(GCNArchTy arch)
{
  return arch == gcn14 ? "v_subb_co_u32" : "v_subb_u32";
}

static void emitAddrCalcInstructions(FILE *hFile, GCNArchTy arch, int addrIdx, int count, int offset, int offsetChange = 0)
{
  if (count == 1) {
    if (addrIdx != 0) {
      fprintf(hFile, "  %s        \\addrv[2], vcc, \\addrv[0], 4*%i\n", add_co_u32(arch), offset);
      fprintf(hFile, "  %s       \\addrv[3], vcc, \\addrv[1], 0, vcc\n", addc_co_u32(arch));
    }
  } else {
    if (addrIdx == 0) {
      fprintf(hFile, "  %s        \\addrv[2], vcc, \\addrv[0], 4*%i\n", add_co_u32(arch), offsetChange);
      fprintf(hFile, "  %s       \\addrv[3], vcc, \\addrv[1], 0, vcc\n", addc_co_u32(arch));
    } else {
      if (4*offset <= 64) {
        fprintf(hFile, "  %s        \\addrv[2], vcc, \\addrv[0], 4*%i\n", add_co_u32(arch), offset);
      } else {
        fprintf(hFile, "  v_mov_b32           \\offset, 4*%i\n", offset);
        fprintf(hFile, "  %s        \\addrv[2], vcc, \\addrv[0], \\offset\n", add_co_u32(arch));
      }
      fprintf(hFile, "  %s       \\addrv[3], vcc, \\addrv[1], 0, vcc\n", addc_co_u32(arch));

      if (4*(offset+offsetChange) <= 64) {
        fprintf(hFile, "  %s        \\addrv[4], vcc, \\addrv[0], 4*%i\n", add_co_u32(arch), offset+offsetChange);
      } else {
        fprintf(hFile, "  v_mov_b32           \\offset, 4*%i\n", offset+offsetChange);
        fprintf(hFile, "  %s        \\addrv[4], vcc, \\addrv[0], \\offset\n", add_co_u32(arch));
      }

      fprintf(hFile, "  %s       \\addrv[5], vcc, \\addrv[1], 0, vcc\n", addc_co_u32(arch));
    }
  }
}

void emitGCNLoad(FILE *hFile, GCNArchTy arch, int opSize)
{
  fprintf(hFile, ".macro load%idw, op, addr, offset, addrv\n", opSize);

  // Calculate base address
  fprintf(hFile, "  %s        \\addrv[0], vcc, \\addr[0], \\offset\n", add_co_u32(arch));
  fprintf(hFile, "  v_mov_b32           \\addrv[1], \\addr[1]\n");
  fprintf(hFile, "  %s       \\addrv[1], vcc, \\addrv[1], 0, vcc\n", addc_co_u32(arch));

  int remaining = opSize;
  int offset = 0;
  int addrIdx = 0;
  while (remaining) {
    addrIdx = (offset == 0) ? 0 : 2;
    int processed = 0;

    if (remaining == 1) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 1, offset);
      fprintf(hFile, "  flat_load_dword   \\op[%i], \\addrv[%i:%i]\n", offset, addrIdx, addrIdx+1);
      processed = 1;
    } else if (remaining == 2) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 1, offset);
      fprintf(hFile, "  flat_load_dwordx2 \\op[%i:%i], \\addrv[%i:%i]\n", offset, offset+1, addrIdx, addrIdx+1);
      processed = 2;
    } else if (remaining == 3) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 2, offset, 2);
      fprintf(hFile, "  flat_load_dwordx2 \\op[%i:%i], \\addrv[%i:%i]\n", offset, offset+1, addrIdx, addrIdx+1);
      fprintf(hFile, "  flat_load_dword   \\op[%i], \\addrv[%i:%i]\n", offset+2, addrIdx+2, addrIdx+3);
      processed = 3;
    } else if (remaining == 4) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 1, offset);
      fprintf(hFile, "  flat_load_dwordx4 \\op[%i:%i], \\addrv[%i:%i]\n", offset, offset+3, addrIdx, addrIdx+1);
      processed = 4;
    } else if (remaining == 5) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 2, offset, 4);
      fprintf(hFile, "  flat_load_dwordx4 \\op[%i:%i], \\addrv[%i:%i]\n", offset, offset+3, addrIdx, addrIdx+1);
      fprintf(hFile, "  flat_load_dword   \\op[%i], \\addrv[%i:%i]\n", offset+4, addrIdx+2, addrIdx+3);
      processed = 5;
    } else if (remaining == 6) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 2, offset, 4);
      fprintf(hFile, "  flat_load_dwordx4 \\op[%i:%i], \\addrv[%i:%i]\n", offset, offset+3, addrIdx, addrIdx+1);
      fprintf(hFile, "  flat_load_dwordx2 \\op[%i:%i], \\addrv[%i:%i]\n", offset+4, offset+5, addrIdx+2, addrIdx+3);
      processed = 6;
    } else if (remaining == 7)  {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 1, offset);
      fprintf(hFile, "  flat_load_dwordx4 \\op[%i:%i], \\addrv[%i:%i]\n", offset, offset+3, addrIdx, addrIdx+1);
      processed = 4;
    } else if (remaining >= 8) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 2, offset, 4);
      fprintf(hFile, "  flat_load_dwordx4 \\op[%i:%i], \\addrv[%i:%i]\n", offset, offset+3, addrIdx, addrIdx+1);
      fprintf(hFile, "  flat_load_dwordx4 \\op[%i:%i], \\addrv[%i:%i]\n", offset+4, offset+7, addrIdx+2, addrIdx+3);
      processed = 8;
    }

    remaining -= processed;
    offset += processed;
  }

  fprintf(hFile, "  s_waitcnt           vmcnt(0)\n");
  fprintf(hFile, ".endm\n\n");
}

void emitGCNStore(FILE *hFile, GCNArchTy arch, int opSize)
{
  fprintf(hFile, ".macro store%idw, op, addr, offset, addrv\n", opSize);

  // Calculate base address
  fprintf(hFile, "  %s        \\addrv[0], vcc, \\addr[0], \\offset\n", add_co_u32(arch));
  fprintf(hFile, "  v_mov_b32           \\addrv[1], \\addr[1]\n");
  fprintf(hFile, "  %s       \\addrv[1], vcc, \\addrv[1], 0, vcc\n", addc_co_u32(arch));

  int remaining = opSize;
  int offset = 0;
  int addrIdx = 0;
  while (remaining) {
    addrIdx = (offset == 0) ? 0 : 2;
    int processed = 0;

    if (remaining == 1) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 1, offset);
      fprintf(hFile, "  flat_store_dword   \\addrv[%i:%i], \\op[%i]\n", addrIdx, addrIdx+1, offset);
      processed = 1;
    } else if (remaining == 2) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 1, offset);
      fprintf(hFile, "  flat_store_dwordx2 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx, addrIdx+1, offset, offset+1);
      processed = 2;
    } else if (remaining == 3) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 2, offset, 2);
      fprintf(hFile, "  flat_store_dwordx2 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx, addrIdx+1, offset, offset+1);
      fprintf(hFile, "  flat_store_dword   \\addrv[%i:%i], \\op[%i]\n", addrIdx+2, addrIdx+3, offset+2);
      processed = 3;
    } else if (remaining == 4) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 1, offset);
      fprintf(hFile, "  flat_store_dwordx4 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx, addrIdx+1, offset, offset+3);
      processed = 4;
    } else if (remaining == 5) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 2, offset, 4);
      fprintf(hFile, "  flat_store_dwordx4 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx, addrIdx+1, offset, offset+3);
      fprintf(hFile, "  flat_store_dword   \\addrv[%i:%i], \\op[%i]\n", addrIdx+2, addrIdx+3, offset+4);
      processed = 5;
    } else if (remaining == 6) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 2, offset, 4);
      fprintf(hFile, "  flat_store_dwordx4 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx, addrIdx+1, offset, offset+3);
      fprintf(hFile, "  flat_store_dwordx2 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx+2, addrIdx+3, offset+4, offset+5);
      processed = 6;
    } else if (remaining == 7)  {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 1, offset);
      fprintf(hFile, "  flat_store_dwordx4 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx, addrIdx+1, offset, offset+3);
      processed = 4;
    } else if (remaining >= 8) {
      emitAddrCalcInstructions(hFile, arch, addrIdx, 2, offset, 4);
      fprintf(hFile, "  flat_store_dwordx4 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx, addrIdx+1, offset, offset+3);
      fprintf(hFile, "  flat_store_dwordx4 \\addrv[%i:%i], \\op[%i:%i]\n", addrIdx+2, addrIdx+3, offset+4, offset+7);
      processed = 8;
    }

    remaining -= processed;
    offset += processed;
  }

  fprintf(hFile, "  s_waitcnt           vmcnt(0)\n");
  fprintf(hFile, ".endm\n\n");
}

void emitGCNLimbShl(FILE *hFile, int opSize)
{
  fprintf(hFile, ".macro limbshl%i, reg\n", opSize);
  for (int i = 0; i < opSize; i++) {
    if (i != opSize-1)
      fprintf(hFile, "  v_mov_b32        \\reg[%i], \\reg[%i]\n", opSize-1-i, opSize-2-i);
    else
      fprintf(hFile, "  v_mov_b32        \\reg[%i], 0\n", opSize-1-i);
  }
  fprintf(hFile, ".endm\n\n");
}

void emitGCNShl(FILE *hFile, GCNArchTy arch, int opSize)
{
  fprintf(hFile, ".macro shl%i, op, count, reg\n", opSize);

  fprintf(hFile, "  v_mov_b32           \\reg[0], 0xFFFFFFFF\n");
  fprintf(hFile, "  v_cmp_eq_u32        vcc, \\count, 0\n");
  fprintf(hFile, "  v_cndmask_b32       \\reg[1], \\reg[0], 0, vcc\n");
  fprintf(hFile, "  %s        \\reg[0], vcc, 32, \\count\n", sub_co_u32(arch));

  for (int i = 0; i < opSize; i++) {
    fprintf(hFile, "  v_lshlrev_b32        \\op[%i], \\count, \\op[%i]\n", opSize-1-i, opSize-1-i);
    if (i != opSize-1) {
      fprintf(hFile, "  v_lshrrev_b32        \\reg[2], \\reg[0], \\op[%i]\n", opSize-2-i);
      fprintf(hFile, "  v_and_b32            \\reg[2], \\reg[1], \\reg[2]\n");
      fprintf(hFile, "  v_or_b32             \\op[%i], \\reg[2], \\op[%i]\n", opSize-1-i, opSize-1-i);
    }
  }
  fprintf(hFile, ".endm\n\n");
}

void emitGCNSubMul(FILE *hFile, GCNArchTy arch, int opSize)
{
  fprintf(hFile, ".macro submul%i, op1, op2, m, reg, sreg, zero\n", opSize*32);

  for (int i = 0; i < opSize; i += 2) {
    fprintf(hFile, "  v_mad_u64_u32       \\reg[0:1], \\sreg[0:1], \\m, \\op2[%i], \\zero[0:1]\n", i);
    if (i == 0)
      fprintf(hFile, "  %s        \\op1[%i], vcc, \\op1[%i], \\reg[0]\n", sub_co_u32(arch), i, i);
    else
      fprintf(hFile, "  %s       \\op1[%i], vcc, \\op1[%i], \\reg[0], vcc\n", subb_co_u32(arch), i, i);

    fprintf(hFile, "  %s       \\op1[%i], vcc, \\op1[%i], \\reg[1], vcc\n", subb_co_u32(arch), i+1, i+1);
  }

  if (opSize % 2 == 0)
    fprintf(hFile, "  %s       \\op1[%i], vcc, \\op1[%i], 0, vcc\n", subb_co_u32(arch), opSize, opSize);


  for (int i = 1; i < opSize; i += 2) {
    fprintf(hFile, "  v_mad_u64_u32       \\reg[0:1], \\sreg[0:1], \\m, \\op2[%i], \\zero[0:1]\n", i);
    if (i == 1)
      fprintf(hFile, "  %s        \\op1[%i], vcc, \\op1[%i], \\reg[0]\n", sub_co_u32(arch), i, i);
    else
      fprintf(hFile, "  %s       \\op1[%i], vcc, \\op1[%i], \\reg[0], vcc\n", subb_co_u32(arch), i, i);

    fprintf(hFile, "  %s       \\op1[%i], vcc, \\op1[%i], \\reg[1], vcc\n", subb_co_u32(arch), i+1, i+1);
  }

  if (opSize % 2 != 0)
    fprintf(hFile, "  %s       \\op1[%i], vcc, \\op1[%i], 0, vcc\n", subb_co_u32(arch), opSize, opSize);

  fprintf(hFile, ".endm\n\n");
}

void emitGCNMul_prodscan(FILE *hFile, GCNArchTy arch, int op1Size, int op2Size, int limit)
{
  if (limit == -1) {
    fprintf(hFile, ".macro mul%uto%i, in1, in2, out, zero\n", op1Size*32, op2Size*32);
    limit = op1Size + op2Size;
  } else {
    fprintf(hFile, ".macro mul%uto%il%i, in1, in2, out, zero\n", op1Size*32, op2Size*32, limit*32);
  }

  for (int i = 0; i < limit; i++) {
    int off1 = std::max(i - op2Size + 1, 0);
    int off2 = i - off1;
    int count = std::min(op1Size-off1, i-off1+1);

    if (off1 == 0 && off2 == 0) {
      fprintf(hFile, "  v_mov_b32          \\out[2], 0\n");
      fprintf(hFile, "  v_mad_u64_u32      \\out[0:1], vcc, \\in1[0], \\in2[0], \\zero[0:1]\n");
    } else {
      for (int j = 0; j < count; j++, off1++, off2--) {
        fprintf(hFile, "  v_mad_u64_u32      \\out[%i:%i], vcc, \\in1[%i], \\in2[%i], \\out[%i:%i]\n", i, i+1, off1, off2, i, i+1);
        if (i+2 < limit) {
          if (j == 0)
            fprintf(hFile, "  %s      \\out[%i], vcc, 0, 0, vcc\n", addc_co_u32(arch), i+2);
          else
            fprintf(hFile, "  %s      \\out[%i], vcc, 0, \\out[%i], vcc\n", addc_co_u32(arch), i+2, i+2);
        }
      }
    }
  }

  fprintf(hFile, ".endm\n\n");
}

void emitGCNSqr_prodscan(FILE *hFile, GCNArchTy arch, int opSize)
{
  fprintf(hFile, ".macro sqr%i, in, out, cache, zero, calc\n", opSize*32);

  for (int i = 0; i < opSize; i++) {
    fprintf(hFile, "  %s         \\cache[%i], vcc, \\in[%i], \\in[%i]\n", add_co_u32(arch), i*2, i, i);
    fprintf(hFile, "  %s        \\cache[%i], vcc, 0, 0, vcc\n", subb_co_u32(arch), i*2+1);
  }

  for (int i = 0; i < 2*opSize; i++) {
    int off1 = std::max(i - opSize + 1, 0);
    int off2 = i - off1;
    int count = std::min(opSize-off1, i-off1+1);

    if (off1 == 0 && off2 == 0) {
      fprintf(hFile, "  v_mad_u64_u32      \\out[0:1], vcc, \\in[0], \\in[0], \\zero[0:1]\n");
    } else {
      for (int j = 0; j < count/2; j++, off1++, off2--) {
        if (i == 1) {
          fprintf(hFile, "  v_and_b32          \\out[%i], \\cache[%i], \\in[%i]\n", i+1, off1*2+1, off2);
        } else {
          fprintf(hFile, "  v_and_b32          \\calc[0], \\cache[%i], \\in[%i]\n", off1*2+1, off2);
          fprintf(hFile, "  %s       \\out[%i], vcc, \\calc[0], \\out[%i]\n", add_co_u32(arch), i+1, i+1);
          if (i+2 < (2*opSize)) {
            if (j == 0)
              fprintf(hFile, "  %s      \\out[%i], vcc, 0, 0, vcc\n", addc_co_u32(arch), i+2);
            else
              fprintf(hFile, "  %s      \\out[%i], vcc, 0, \\out[%i], vcc\n", addc_co_u32(arch), i+2, i+2);
          }
        }

        fprintf(hFile, "  v_mad_u64_u32      \\out[%i:%i], vcc, \\cache[%i], \\in[%i], \\out[%i:%i]\n", i, i+1, off1*2, off2, i, i+1);
        if (i+2 < (2*opSize)) {
          if (i == 1)
            fprintf(hFile, "  %s      \\out[%i], vcc, 0, 0, vcc\n", addc_co_u32(arch), i+2);
          else
            fprintf(hFile, "  %s      \\out[%i], vcc, 0, \\out[%i], vcc\n", addc_co_u32(arch), i+2, i+2);
        }
      }

      if (count & 1) {
        fprintf(hFile, "  v_mad_u64_u32      \\out[%i:%i], vcc, \\in[%i], \\in[%i], \\out[%i:%i]\n", i, i+1, off1, off2, i, i+1);
        if (i+2 < (2*opSize)) {
          fprintf(hFile, "  %s      \\out[%i], vcc, 0, \\out[%i], vcc\n", addc_co_u32(arch), i+2, i+2);
        }
      }
    }
  }

  fprintf(hFile, ".endm\n\n");
}

void emitGCNMonsqr_prodscan(FILE *hFile, GCNArchTy arch, int opSize, bool debug)
{
  fprintf(hFile, ".macro monsqr%i, m1ctx, mod, inv, invm, cache, zero, x, sregs\n", opSize*32);

  // struct {
  //   uint32_t calc[3];
  //   uint32_t m1[N];
  // } m1ctx;

  int operandDataOffset = 3;

  for (int i = 0; i < opSize; i++) {
    fprintf(hFile, "  %s         \\cache[%i], vcc, \\m1ctx[%i], \\m1ctx[%i]\n", add_co_u32(arch), i*2, operandDataOffset+i, operandDataOffset+i);
    fprintf(hFile, "  %s        \\cache[%i], vcc, 0, 0, vcc\n", subb_co_u32(arch), i*2+1);
  }

  for (int i = 0; i < 2*opSize; i++) {
    int outIdx = std::max(0, i - opSize + 3);
    int count = opSize - abs(opSize - i - 1);

    int off1 = std::max(i - opSize + 1, 0);
    int off2 = i - off1;

    if (debug)
      fprintf(hFile, "\n  #; result limb %i out index: %i\n", i, outIdx);

    if (i == 2*opSize - 4) {
      fprintf(hFile, "  #; move 2 last limbs to begin of m1ctx\n");
      fprintf(hFile, "  v_mov_b32          \\m1ctx[0], \\m1ctx[%i]\n", operandDataOffset+opSize-2);
      fprintf(hFile, "  v_mov_b32          \\m1ctx[1], \\m1ctx[%i]\n", operandDataOffset+opSize-1);
    }

    if (off1 == 0 && off2 == 0) {
      fprintf(hFile, "  v_mad_u64_u32      \\m1ctx[0:1], vcc, \\m1ctx[%i], \\m1ctx[%i], \\zero[0:1]\n", operandDataOffset, operandDataOffset);
    } else {
      for (int j = 0; j < count/2; j++, off1++, off2--) {
        int op2Idx = ((i >= 2*opSize-4) && off2 >= (opSize-2)) ? off2 - (opSize-2) : operandDataOffset+off2;

        if (outIdx == 0 && j == 0) {
          fprintf(hFile, "  v_mad_u64_u32      \\m1ctx[0:1], vcc, \\cache[%i], \\m1ctx[%i], \\m1ctx[1:2]\n", off1*2, op2Idx);
        } else {
          fprintf(hFile, "  v_mad_u64_u32      \\m1ctx[%i:%i], vcc, \\cache[%i], \\m1ctx[%i], \\m1ctx[%i:%i]\n", outIdx, outIdx+1, off1*2, op2Idx, outIdx, outIdx+1);
        }

        if (i > 0 && i+2 < (2*opSize)) {
          if (j == 0)
            fprintf(hFile, "  %s      \\m1ctx[%i], vcc, 0, 0, vcc\n", addc_co_u32(arch), outIdx+2);
          else
            fprintf(hFile, "  %s      \\m1ctx[%i], vcc, 0, \\m1ctx[%i], vcc\n", addc_co_u32(arch), outIdx+2, outIdx+2);
        }

        fprintf(hFile, "  v_and_b32          \\x, \\cache[%i], \\m1ctx[%i]\n", off1*2+1, op2Idx);
        fprintf(hFile, "  %s       \\m1ctx[%i], vcc, \\x, \\m1ctx[%i]\n", add_co_u32(arch), outIdx+1, outIdx+1);
        if (i > 0 && i+2 < (2*opSize)) {
          fprintf(hFile, "  %s      \\m1ctx[%i], vcc, 0, \\m1ctx[%i], vcc\n", addc_co_u32(arch), outIdx+2, outIdx+2);
        }
      }

      if (count & 1) {
        int op1Idx = ((i >= 2*opSize-4) && off1 >= (opSize-2)) ? off1 - (opSize-2) : operandDataOffset+off1;
        int op2Idx = ((i >= 2*opSize-4) && off2 >= (opSize-2)) ? off2 - (opSize-2) : operandDataOffset+off2;
        fprintf(hFile, "  v_mad_u64_u32      \\m1ctx[%i:%i], vcc, \\m1ctx[%i], \\m1ctx[%i], \\m1ctx[%i:%i]\n", outIdx, outIdx+1, op1Idx, op2Idx, outIdx, outIdx+1);
        if (i+2 < (2*opSize)) {
          fprintf(hFile, "  %s      \\m1ctx[%i], vcc, 0, \\m1ctx[%i], vcc\n", addc_co_u32(arch), outIdx+2, outIdx+2);
        } else if (i == (2*opSize - 2)) {
          fprintf(hFile, "  %s      \\x, vcc, 0, 0, vcc\n", addc_co_u32(arch));
        }
      }
    }

    // Inversion
    off1 = std::max(i - opSize + 1, 0);
    off2 = i - off1;
    if (off1 == 0 && off2 == 0) {
      fprintf(hFile, "  v_mul_lo_u32       \\inv[0], \\m1ctx[0], \\invm\n");
      fprintf(hFile, "  v_mad_u64_u32      \\m1ctx[0:1], vcc, \\inv[0], \\mod[0], \\m1ctx[0:1]\n");
      fprintf(hFile, "  %s      \\m1ctx[2], vcc, 0, 0, vcc\n", addc_co_u32(arch));
    } else {
      for (int j = 0; j < count; j++, off1++, off2--) {
        if (i < opSize && off1 == i)
          fprintf(hFile, "  v_mul_lo_u32       \\inv[%i], \\m1ctx[%i], \\invm\n", i, outIdx);
        fprintf(hFile, "  v_mad_u64_u32      \\m1ctx[%i:%i], vcc, \\inv[%i], \\mod[%i], \\m1ctx[%i:%i]\n", outIdx, outIdx+1, off1, off2, outIdx, outIdx+1);
        if (i+2 < (2*opSize)) {
          fprintf(hFile, "  %s      \\m1ctx[%i], vcc, 0, \\m1ctx[%i], vcc\n", addc_co_u32(arch), outIdx+2, outIdx+2);
        } else if (i == (2*opSize - 2)) {
          fprintf(hFile, "  %s      \\x, vcc, 0, \\x, vcc\n", addc_co_u32(arch));
        }
      }
    }
  }

  fprintf(hFile, "  s_mov_b64         \\sregs, exec\n");
  fprintf(hFile, "  v_cmpx_gt_u32     vcc, \\x, 0\n");
  for (int i = 0; i < opSize; i++) {
    if (i == 0)
      fprintf(hFile, "  %s      \\m1ctx[%i], vcc, \\m1ctx[%i], \\mod[%i]\n", sub_co_u32(arch), i+operandDataOffset, i+operandDataOffset, i);
    else
      fprintf(hFile, "  %s     \\m1ctx[%i], vcc, \\m1ctx[%i], \\mod[%i], vcc\n", subb_co_u32(arch), i+operandDataOffset, i+operandDataOffset, i);
  }

  fprintf(hFile, "  s_mov_b64         exec, \\sregs\n");
  fprintf(hFile, ".endm\n\n");
}

void emitGCNMonmul_prodscan(FILE *hFile, GCNArchTy arch, int opSize, bool debug)
{
  fprintf(hFile, ".macro monmul%i, m1, m2, mod, inv, invm, cache, zero, x, sregs\n", opSize*32);

  for (int i = 0; i < opSize; i++)
    fprintf(hFile, "  v_mov_b32           \\cache[%i], \\m1[%i]\n", i, i);

  for (int i = 0; i < 2*opSize; i++) {
    int off1 = std::max(i - opSize + 1, 0);
    int off2 = i - off1;
    int count = std::min(opSize-off1, i-off1+1);

    int outIdx = std::max(0, i - opSize);

    if (debug)
      fprintf(hFile, "\n  #; result limb %i out index: %i\n", i, outIdx);

    if (off1 == 0 && off2 == 0) {
      fprintf(hFile, "  v_mad_u64_u32      \\m1[0:1], vcc, \\cache[%i], \\m2[%i], \\zero[0:1]\n", 0, 0);
    } else {
      for (int j = 0; j < count; j++, off1++, off2--) {
        int op1Idx = off1;
        int op2Idx = off2;

        if (outIdx == 0 && j == 0) {
          fprintf(hFile, "  v_mad_u64_u32      \\m1[0:1], vcc, \\cache[%i], \\m2[%i], \\m1[1:2]\n", op1Idx, op2Idx);
        } else {
          fprintf(hFile, "  v_mad_u64_u32      \\m1[%i:%i], vcc, \\cache[%i], \\m2[%i], \\m1[%i:%i]\n", outIdx, outIdx+1, op1Idx, op2Idx, outIdx, outIdx+1);
        }

        if (i > 0 && i+2 < (2*opSize)) {
          if (j == 0)
            fprintf(hFile, "  %s      \\m1[%i], vcc, 0, 0, vcc\n", addc_co_u32(arch), outIdx+2);
          else
            fprintf(hFile, "  %s      \\m1[%i], vcc, 0, \\m1[%i], vcc\n", addc_co_u32(arch), outIdx+2, outIdx+2);
        } else if (i == (2*opSize - 2)) {
          fprintf(hFile, "  %s      \\x, vcc, 0, 0, vcc\n", addc_co_u32(arch));
        }
      }
    }

    // Inversion
    off1 = std::max(i - opSize + 1, 0);
    off2 = i - off1;
    if (off1 == 0 && off2 == 0) {
      fprintf(hFile, "  v_mul_lo_u32       \\inv[0], \\m1[0], \\invm\n");
      fprintf(hFile, "  v_mad_u64_u32      \\m1[0:1], vcc, \\inv[0], \\mod[0], \\m1[0:1]\n");
      fprintf(hFile, "  %s      \\m1[2], vcc, 0, 0, vcc\n", addc_co_u32(arch));
    } else {
      for (int j = 0; j < count; j++, off1++, off2--) {
        if (i < opSize && off1 == i)
          fprintf(hFile, "  v_mul_lo_u32       \\inv[%i], \\m1[%i], \\invm\n", i, outIdx);
        fprintf(hFile, "  v_mad_u64_u32      \\m1[%i:%i], vcc, \\inv[%i], \\mod[%i], \\m1[%i:%i]\n", outIdx, outIdx+1, off1, off2, outIdx, outIdx+1);
        if (i+2 < (2*opSize)) {
          fprintf(hFile, "  %s    \\m1[%i], vcc, 0, \\m1[%i], vcc\n", addc_co_u32(arch), outIdx+2, outIdx+2);
        } else if (i == (2*opSize - 2)) {
          fprintf(hFile, "  %s      \\x, vcc, 0, \\x, vcc\n", addc_co_u32(arch));
        }
      }
    }
  }

  fprintf(hFile, "  s_mov_b64         \\sregs, exec\n");
  fprintf(hFile, "  v_cmpx_gt_u32     vcc, \\x, 0\n");
  for (int i = 0; i < opSize; i++) {
    if (i == 0)
      fprintf(hFile, "  %s      \\m1[%i], vcc, \\m1[%i], \\mod[%i]\n", sub_co_u32(arch), i, i, i);
    else
      fprintf(hFile, "  %s     \\m1[%i], vcc, \\m1[%i], \\mod[%i], vcc\n", subb_co_u32(arch), i, i, i);
  }

  fprintf(hFile, "  s_mov_b64         exec, \\sregs\n");

  fprintf(hFile, ".endm\n\n");
}

void emitGCNRedchalf_prodscan(FILE *hFile, GCNArchTy arch, int opSize, bool carryCheckEnabled, bool debug)
{
  fprintf(hFile, ".macro redchalf%i, m1, mod, inv, invm, cache, zero, x\n", opSize*32);

  for (int i = 0; i < opSize; i++)
    fprintf(hFile, "  v_mov_b32           \\cache[%i], \\m1[%i]\n", i, i);

  for (int i = 0; i < 2*opSize; i++) {
    int off1 = std::max(i - opSize + 1, 0);
    int off2 = i - off1;
    int count = std::min(opSize-off1, i-off1+1);

    int outIdx = std::max(0, i - opSize);

    if (debug)
      fprintf(hFile, "\n  #; result limb %i out index: %i\n", i, outIdx);

    // Inversion
    off1 = std::max(i - opSize + 1, 0);
    off2 = i - off1;
    if (off1 == 0 && off2 == 0) {
      fprintf(hFile, "  v_mul_lo_u32       \\inv[0], \\m1[0], \\invm\n");
      fprintf(hFile, "  v_mad_u64_u32      \\m1[0:1], vcc, \\inv[0], \\mod[0], \\cache[0:1]\n");
      fprintf(hFile, "  %s      \\m1[2], vcc, 0, 0, vcc\n", addc_co_u32(arch));
    } else {
      for (int j = 0; j < count; j++, off1++, off2--) {
        if (i < opSize-1 && j == 1) {
          fprintf(hFile, "  #; add limb %i from m1\n", i);
          fprintf(hFile, "  %s       \\m1[%i], vcc, \\cache[%i], \\m1[%i]\n", add_co_u32(arch), outIdx+1, i+1, outIdx+1);
          fprintf(hFile, "  %s      \\m1[%i], vcc, 0, \\m1[%i], vcc\n", addc_co_u32(arch), outIdx+2, outIdx+2);
        }

        if (i < opSize && off1 == i)
          fprintf(hFile, "  v_mul_lo_u32       \\inv[%i], \\m1[%i], \\invm\n", i, outIdx);

        if (outIdx == 0 && j == 0) {
          fprintf(hFile, "  v_mad_u64_u32      \\m1[0:1], vcc, \\inv[%i], \\mod[%i], \\m1[1:2]\n", off1, off2);
        } else {
          fprintf(hFile, "  v_mad_u64_u32      \\m1[%i:%i], vcc, \\inv[%i], \\mod[%i], \\m1[%i:%i]\n", outIdx, outIdx+1, off1, off2, outIdx, outIdx+1);
        }

        if (i > 0 && i+2 < (2*opSize)) {
          if (j == 0)
            fprintf(hFile, "  %s      \\m1[%i], vcc, 0, 0, vcc\n", addc_co_u32(arch), outIdx+2);
          else
            fprintf(hFile, "  %s      \\m1[%i], vcc, 0, \\m1[%i], vcc\n", addc_co_u32(arch), outIdx+2, outIdx+2);
        }
      }
    }
  }

  if (carryCheckEnabled) {
    // Impossible to have carry there
    fprintf(hFile, "  v_mov_b32         \\cache[0], 0xFFFFFFFF\n");
    fprintf(hFile, "  v_cndmask_b32     \\x, 0, \\cache[0], vcc\n");
    for (int i = 0; i < opSize; i++) {
      fprintf(hFile, "  v_and_b32         \\cache[%i], \\x, \\mod[%i]\n", i, i);
    }

    for (int i = 0; i < opSize; i++) {
      if (i == 0)
        fprintf(hFile, "  %s      \\m1[%i], vcc, \\m1[%i], \\cache[%i]\n", sub_co_u32(arch), i, i, i);
      else
        fprintf(hFile, "  %s     \\m1[%i], vcc, \\m1[%i], \\cache[%i], vcc\n", subb_co_u32(arch), i, i, i);
    }
  }

  fprintf(hFile, ".endm\n\n");
}

void emitGCNRedcify_prodscan(FILE *hFile, GCNArchTy arch, int opSize, int windowSize, bool debug)
{
  fprintf(hFile, ".macro redcify%iws%i, quotient, mod, N, cache, out, reg, zero\n", opSize*32, windowSize);

  // reg[0] = shiftCount
  // reg[1] = shiftByLimbCount
  fprintf(hFile, "  %s        \\reg[0], vcc, (1 << %i), \\N\n", sub_co_u32(arch), windowSize);
  fprintf(hFile, "  v_lshrrev_b32       \\reg[1], 5, \\reg[0]\n");

  for (int i = 0; i < (1 << windowSize)/32; i++) {
    if (debug) {
      fprintf(hFile, "  #; shift by limb\n");
      fprintf(hFile, "  #; TODO: try optimize this on Vega\n");
    }
    fprintf(hFile, "  v_cmp_gt_u32        vcc, \\reg[1], %i\n", i);
    for (int j = 0; j < 8-i; j++) {
      if (i == 0) {
        if (j < 8-i-1)
          fprintf(hFile, "  v_cndmask_b32       \\cache[%i], \\quotient[%i], \\quotient[%i], vcc\n", j, j, j+1);
        else
          fprintf(hFile, "  v_cndmask_b32       \\cache[%i], \\quotient[%i], 0, vcc\n", j, j);
      } else {
        if (j < 8-i-1)
          fprintf(hFile, "  v_cndmask_b32       \\cache[%i], \\cache[%i], \\cache[%i], vcc\n", j, j, j+1);
        else
          fprintf(hFile, "  v_cndmask_b32       \\cache[%i], \\cache[%i], 0, vcc\n", j, j);
      }
    }
  }

  // shift
  // reg[0] = shiftCount
  // reg[1] = 32-shiftCount
  // reg[2] = mask
  // reg[3] = tmp
  fprintf(hFile, "  v_mov_b32           \\reg[1], 0xFFFFFFFF\n");
  fprintf(hFile, "  v_and_b32           \\reg[0], 0x1F, \\reg[0]\n");
  fprintf(hFile, "  v_cmp_eq_u32        vcc, \\reg[0], 0\n");
  fprintf(hFile, "  v_cndmask_b32       \\reg[2], \\reg[1], 0, vcc\n");
  fprintf(hFile, "  %s        \\reg[1], vcc, 32, \\reg[0]\n", sub_co_u32(arch));

  for (unsigned i = 0; i < 8; i++) {
    fprintf(hFile, "  v_lshrrev_b32       \\cache[%i], \\reg[0], \\cache[%i]\n", i, i);
    if (i < 8-1) {
      fprintf(hFile, "  v_lshlrev_b32       \\reg[3], \\reg[1], \\cache[%i]\n", i+1);
      fprintf(hFile, "  v_and_b32           \\reg[3], \\reg[2], \\reg[3]\n");
      fprintf(hFile, "  v_or_b32            \\cache[%i], \\reg[3], \\cache[%i]\n", i, i);
    }
  }

  fprintf(hFile, "  mul%ito%il%i     \\mod, \\cache, \\out, \\zero\n", opSize*32, 64 + (1 << windowSize), opSize*32);

  for (int i = 0; i < opSize; i++)
    fprintf(hFile, "  v_not_b32           \\out[%i], \\out[%i]\n", i, i);

  fprintf(hFile, "  %s        \\out[0], vcc, 1, \\out[0]\n", add_co_u32(arch));

  fprintf(hFile, ".endm\n\n");
}

void emitGCNDiv(FILE *hFile, GCNArchTy arch, int dividendSize, int divisorSize, int quotientSize, bool debug)
{
  fprintf(hFile, ".macro div%ito%i, dividend, divisor, quotient, size, reg, sreg, zero\n", dividendSize*32, divisorSize*32);
  fprintf(hFile, "      dividendSize = %%\\reg[16]\n");
  fprintf(hFile, "       divisorSize = %%\\reg[17]\n");
  fprintf(hFile, "         cyclesNum = %%\\reg[18]\n");
  fprintf(hFile, "        shiftCount = %%\\reg[19]\n");
  fprintf(hFile, "                 q = %%\\reg[20]\n");
  fprintf(hFile, "            borrow = %%\\reg[21]\n");

  // fill quotient with zeroes
  for (int i = 0; i < quotientSize; i++)
    fprintf(hFile, "  v_mov_b32           \\quotient[%i], 0\n", i);

  fprintf(hFile, "  v_mov_b32           dividendSize, (%i/32)\n", dividendSize*32);
  fprintf(hFile, "  v_mov_b32           divisorSize, (%i/32)\n", divisorSize*32);

  // save exec
  if (debug)
    fprintf(hFile, "  #; save original exec to sreg[0:1]\n");
  fprintf(hFile, "  s_mov_b64            \\sreg[0:1], exec\n");

  fprintf(hFile, ".div_divisornormalize_loop:\n");
  fprintf(hFile, "  v_cmpx_eq_u32       vcc, \\divisor[%i], 0\n", divisorSize-1);
  fprintf(hFile, "  s_cbranch_execz     .div_divisornormalize_loop_end\n");
  fprintf(hFile, "  limbshl%i           \\divisor\n", divisorSize);
  fprintf(hFile, "  %s           divisorSize, vcc, divisorSize, 1\n", sub_co_u32(arch));
  fprintf(hFile, "  s_branch            .div_divisornormalize_loop\n");

  fprintf(hFile, ".div_divisornormalize_loop_end:\n");
  fprintf(hFile, "  s_mov_b64           exec, \\sreg[0:1]\n");
  fprintf(hFile, "  v_ffbh_u32          shiftCount, \\divisor[%i]\n", divisorSize-1);
  fprintf(hFile, "  shl%i               \\divisor, shiftCount, \\reg\n", divisorSize);
  fprintf(hFile, "  shl%i               \\dividend, shiftCount, \\reg\n", dividendSize);

  fprintf(hFile, ".div_dividendnormalize_loop:\n");
  fprintf(hFile, "  v_cmpx_eq_u32       vcc, \\dividend[%i], 0\n", dividendSize-1);
  fprintf(hFile, "  s_cbranch_execz     .div_dividendnormalize_loop_end\n");
  fprintf(hFile, "  limbshl%i           \\dividend\n", dividendSize);
  fprintf(hFile, "  %s           dividendSize, vcc, dividendSize, 1\n", sub_co_u32(arch));
  fprintf(hFile, "  s_branch            .div_dividendnormalize_loop\n");

  fprintf(hFile, ".div_dividendnormalize_loop_end:\n");
  fprintf(hFile, "  s_mov_b64           exec, \\sreg[0:1]\n");
  fprintf(hFile, "  %s           \\reg[0], vcc, dividendSize, divisorSize\n", sub_co_u32(arch));
  fprintf(hFile, "  v_min_u32           cyclesNum, \\reg[0], %i\n", quotientSize);

  fprintf(hFile, ".div_mainloop:\n");
  fprintf(hFile, "  v_cmpx_gt_u32       vcc, cyclesNum, 0\n");
  fprintf(hFile, "  s_cbranch_execz     .div_mainloop_end\n");
  fprintf(hFile, "  #; make division highest 64-bit dividend bits to highest 32-bit divisor bits\n");
  fprintf(hFile, "      dividendhi = %%\\dividend[%i:%i]\n", dividendSize-2, dividendSize-1);
  fprintf(hFile, "  div64to32           dividendhi, \\divisor[%i], q, \\reg, \\sreg[2:3]\n", divisorSize-1);
  fprintf(hFile, "  v_mov_b32           \\reg[0], 0xFFFFFFFF\n");
  fprintf(hFile, "  v_cmp_eq_u32        vcc, \\dividend[%i], \\divisor[%i]\n", dividendSize-1, divisorSize-1);
  fprintf(hFile, "  v_cndmask_b32       q, q, \\reg[0], vcc\n");

  fprintf(hFile, "      dividendhi = %%\\dividend[%i:%i]\n", dividendSize - divisorSize - 1, dividendSize-1);
  fprintf(hFile, "      carryholder = %%\\sreg[2:3]\n");
  fprintf(hFile, "  submul%i           dividendhi, \\divisor, q, \\reg, carryholder, \\zero\n", divisorSize*32);
  fprintf(hFile, "  v_mov_b32           borrow, \\dividend[%i]\n", dividendSize-1);
  fprintf(hFile, "  limbshl%i           \\dividend\n", dividendSize);
  fprintf(hFile, "  s_mov_b64           \\sreg[2:3], exec\n");

  fprintf(hFile, "  v_cmpx_lg_u32       vcc, borrow, 0\n");
  for (int i = 0; i < divisorSize; i++) {
    if (i == 0)
      fprintf(hFile, "  %s        \\dividend[%i], vcc, \\dividend[%i], \\divisor[%i]\n", add_co_u32(arch), dividendSize-divisorSize+i, dividendSize-divisorSize+i, i);
    else
      fprintf(hFile, "  %s       \\dividend[%i], vcc, \\dividend[%i], \\divisor[%i], vcc\n", addc_co_u32(arch), dividendSize-divisorSize+i, dividendSize-divisorSize+i, i);
  }
  fprintf(hFile, "  %s           q, vcc, q, 1\n", sub_co_u32(arch));

  fprintf(hFile, "  v_cmpx_gt_u32       vcc, \\dividend[%i], \\divisor[%i]\n", dividendSize-1, divisorSize-1);
  for (int i = 0; i < divisorSize; i++) {
    if (i == 0)
      fprintf(hFile, "  %s        \\dividend[%i], vcc, \\dividend[%i], \\divisor[%i]\n", add_co_u32(arch), dividendSize-divisorSize+i, dividendSize-divisorSize+i, i);
    else
      fprintf(hFile, "  %s       \\dividend[%i], vcc, \\dividend[%i], \\divisor[%i], vcc\n", addc_co_u32(arch), dividendSize-divisorSize+i, dividendSize-divisorSize+i, i);
  }
  fprintf(hFile, "  %s           q, vcc, q, 1\n", sub_co_u32(arch));

  fprintf(hFile, "  s_mov_b64           exec, \\sreg[2:3]\n");
  fprintf(hFile, "  limbshl%i           \\quotient\n", quotientSize);
  fprintf(hFile, "  v_mov_b32           \\quotient[0], q\n");

  fprintf(hFile, "  %s           cyclesNum, vcc, cyclesNum, 1\n", sub_co_u32(arch));
  fprintf(hFile, "  s_branch            .div_mainloop\n");

  fprintf(hFile, ".div_mainloop_end:\n");
  fprintf(hFile, "  s_mov_b64           exec, \\sreg[0:1]\n");
  fprintf(hFile, "  %s           \\reg[1], vcc, 320, shiftCount\n", sub_co_u32(arch));

  fprintf(hFile, "  %s           \\reg[0], vcc, 10, divisorSize\n", sub_co_u32(arch));
  fprintf(hFile, "  v_lshlrev_b32       \\reg[0], 5, \\reg[0]\n");
  fprintf(hFile, "  %s           \\size, vcc, \\reg[1], \\reg[0]\n", sub_co_u32(arch));

  fprintf(hFile, ".endm\n\n");
}

void emitGCNModPow2(FILE *hFile, GCNArchTy arch, int opSize, bool debug)
{
  fprintf(hFile, ".macro modpow%iof2, mod, regs, zero, sregs\n", opSize*32);
  int offset = 0;
  fprintf(hFile, "     # common\n");
  fprintf(hFile, "         modpowof2_coeff    = %%\\regs[%i:%i]\n", offset, offset+8-1);
    offset += 8;
  fprintf(hFile, "         modpowof2_bitsize  = %%\\regs[%i]\n", offset);
    offset++;

  int off1 = offset;
  fprintf(hFile, "     # divide time\n");
  fprintf(hFile, "         modpowof2_dividend = %%\\regs[%i:%i]\n", offset, offset+opSize+5-1);
    offset += opSize+5;
  fprintf(hFile, "         modpowof2_divisor  = %%\\regs[%i:%i]\n", offset, offset+opSize-1);
    offset += opSize;
  fprintf(hFile, "         modpowof2_dreg     = %%\\regs[%i:%i]\n", offset, offset+22);

  offset = off1;
  fprintf(hFile, "    # modpow time\n");
  fprintf(hFile, "        modpowof2_e         = %%\\regs[%i:%i]\n", offset, offset+opSize-1);
    offset += opSize;
  fprintf(hFile, "        modpowof2_resultacc = %%\\regs[%i:%i]\n", offset, offset+3+opSize+1-1);
  fprintf(hFile, "        modpowof2_result    = %%\\regs[%i:%i]\n", offset+3, offset+3+opSize+1-1);
    offset += 3+opSize+1;
  fprintf(hFile, "        modpowof2_inv       = %%\\regs[%i:%i]\n", offset, offset+opSize-1);
    offset += opSize;
  fprintf(hFile, "        modpowof2_cache     = %%\\regs[%i:%i]\n", offset, offset+2*opSize-1);
  fprintf(hFile, "          modpowof2_redcifyout   = %%\\regs[%i:%i]\n", offset, offset+opSize+1-1);
  fprintf(hFile, "          modpowof2_redcifycache = %%\\regs[%i:%i]\n", offset+2*opSize-1-7, offset+2*opSize-1);
  fprintf(hFile, "          modpowof2_mulcache     = %%\\regs[%i:%i]\n", offset+opSize, offset+2*opSize-1);
    offset += 2*opSize;
  fprintf(hFile, "        modpowof2_invm      = %%\\regs[%i]\n", offset);
    offset++;
  fprintf(hFile, "        modpowof2_count     = %%\\regs[%i]\n", offset);
    offset++;
  fprintf(hFile, "        modpowof2_wsReg     = %%\\regs[%i]\n", offset);
    offset++;
  fprintf(hFile, "        modpowof2_mreg      = %%\\regs[%i:%i]\n", offset, offset+4-1);
    offset += 4;
  fprintf(hFile, "    # scalars\n");
  fprintf(hFile, "        modpow_exec0        = %%\\sregs[0:1]\n");
  fprintf(hFile, "        modpow_exec1        = %%\\sregs[2:3]\n");
  fprintf(hFile, "        modpow_exec2        = %%\\sregs[4:5]\n");
  fprintf(hFile, "\n");

  fprintf(hFile, "  #; fill dividend & divisor; calculate modpowof2_coeff\n");
  for (int i = 0; i < opSize+5; i++) {
    if (i != opSize+5-1)
      fprintf(hFile, "  v_mov_b32           modpowof2_dividend[%i], 0\n", i);
    else
      fprintf(hFile, "  v_mov_b32           modpowof2_dividend[%i], 1\n", i);
  }
  for (int i = 0; i < opSize; i++)
      fprintf(hFile, "  v_mov_b32           modpowof2_divisor[%i], \\mod[%i]\n", i, i);
  fprintf(hFile, "  div%ito%i         modpowof2_dividend, modpowof2_divisor, modpowof2_coeff, modpowof2_bitsize, modpowof2_dreg, \\sregs, \\zero\n", (opSize+5)*32, opSize*32);

  fprintf(hFile, "\n  #; copy normalized mod to e\n");
  for (int i = 0; i < opSize; i++)
      fprintf(hFile, "  v_mov_b32           modpowof2_e[%i], modpowof2_divisor[%i]\n", i, i);

  fprintf(hFile, "\n  #; calculate 2 in Montgomery representation\n");
  fprintf(hFile, "  v_mov_b32           modpowof2_mreg[0], 1\n");
  fprintf(hFile, "  redcify%iws7       modpowof2_coeff, \\mod, modpowof2_mreg[0], modpowof2_redcifycache, modpowof2_result, modpowof2_mreg, \\zero\n", opSize*32);

  fprintf(hFile, "\n  #; invert first limb\n");
  fprintf(hFile, "  invert_limb         \\mod[0], modpowof2_cache, modpowof2_mreg, modpowof2_invm\n");

  fprintf(hFile, "\n  #; prepare main loop\n");
  fprintf(hFile, "  %s           modpowof2_bitsize, vcc, modpowof2_bitsize, 1\n", sub_co_u32(arch));
  fprintf(hFile, "  v_mov_b32           modpowof2_wsReg, WindowSize\n");
  fprintf(hFile, "  shl%i               modpowof2_e, 1, modpowof2_mreg\n", opSize);
  fprintf(hFile, "  s_mov_b64           modpow_exec0, exec\n");

  fprintf(hFile, "\n.modpowof2.mainloop:\n");
  fprintf(hFile, "  v_min_u32           modpowof2_count, modpowof2_wsReg, modpowof2_bitsize\n");
  fprintf(hFile, "  v_cmpx_lg_u32       vcc, modpowof2_count, 0\n");
  fprintf(hFile, "  s_cbranch_execz     .modpowof2.mainloop.end\n");
  fprintf(hFile, "  %s           modpowof2_bitsize, vcc, modpowof2_bitsize, modpowof2_count\n", sub_co_u32(arch));

  fprintf(hFile, "  %s           modpowof2_mreg[1], vcc, 32, modpowof2_count\n", sub_co_u32(arch));
  fprintf(hFile, "  v_cmp_eq_u32        vcc, modpowof2_bitsize, 0\n");
  fprintf(hFile, "  %s       modpowof2_mreg[2], vcc, 0, 0, vcc\n", addc_co_u32(arch));
  fprintf(hFile, "  v_lshrrev_b32       modpowof2_mreg[3], modpowof2_mreg[1], modpowof2_e[%i]\n", opSize-1);
  fprintf(hFile, "  %s           modpowof2_mreg[3], vcc, modpowof2_mreg[3], modpowof2_mreg[2]\n", sub_co_u32(arch));

  fprintf(hFile, "\n  #; Montgomery square\n");
  fprintf(hFile, "  s_mov_b64           modpow_exec1, exec\n");

  fprintf(hFile, "\n.modpowof2.squareloop:\n");
  fprintf(hFile, "  v_cmpx_lg_u32       vcc, modpowof2_count, 0\n");
  fprintf(hFile, "  s_cbranch_execz     .modpowof2.squareloop_end\n");
  fprintf(hFile, "  %s           modpowof2_count, vcc, modpowof2_count, 1\n", sub_co_u32(arch));
  fprintf(hFile, "  monsqr%i           modpowof2_resultacc, \\mod, modpowof2_inv, modpowof2_invm, modpowof2_cache, \\zero, modpowof2_mreg[0], modpow_exec2\n", opSize*32);
  fprintf(hFile, "  s_branch            .modpowof2.squareloop\n");

  fprintf(hFile, "\n.modpowof2.squareloop_end:\n");
  fprintf(hFile, "  s_mov_b64           exec, modpow_exec1\n");
  fprintf(hFile, "  redcify%iws7       modpowof2_coeff, \\mod, modpowof2_mreg[3], modpowof2_redcifycache, modpowof2_redcifyout, modpowof2_mreg, \\zero\n", opSize*32);
  fprintf(hFile, "  monmul%i           modpowof2_result, modpowof2_redcifyout, \\mod, modpowof2_inv, modpowof2_invm, modpowof2_mulcache, \\zero, modpowof2_mreg[0], modpow_exec1\n", opSize*32);
  fprintf(hFile, "  shl%i               modpowof2_e, WindowSize, modpowof2_mreg\n", opSize);
  fprintf(hFile, "  s_branch            .modpowof2.mainloop\n");

  fprintf(hFile, ".modpowof2.mainloop.end:\n");
  fprintf(hFile, "  s_mov_b64           exec, modpow_exec0\n");
  fprintf(hFile, "  redchalf%i         modpowof2_result, \\mod, modpowof2_inv, modpowof2_invm, modpowof2_cache, \\zero, modpowof2_mreg[0]\n", opSize*32);

  for (int i = 0; i < opSize; i++)
    fprintf(hFile, "  v_mov_b32           \\mod[%i], modpowof2_result[%i]\n", i, i);
  fprintf(hFile, ".endm\n\n");
}
