#include "codegen/generic.h"
#include <algorithm>

static void sp(FILE *hFile, int spacesNum)
{
  for (int i = 0; i < spacesNum; i++)
    fprintf(hFile, " ");
}

static void generateShl32(FILE *hFile, int spacesNum, const char *name, int size, const char *term = "0")
{
  for (int i = size; i > 1; i--) {
    sp(hFile, spacesNum); fprintf(hFile, "%s%i = %s%i;\n", name, i-1, name, i-2);
  }
  sp(hFile, spacesNum); fprintf(hFile, "%s0 = %s;\n", name, term);
}

static void generateShl(FILE *hFile, int spacesNum, const char *name, const char *shiftCountName, int size)
{
  for (int i = size-1; i > 0; i--) {
    for (int sp = 0; sp < spacesNum; sp++)
      fprintf(hFile, " ");
    fprintf(hFile, "%s%i = (%s%i << %s) | (%s%i >> (32-%s));\n", name, i, name, i, shiftCountName, name, i-1, shiftCountName);
  }
  for (int sp = 0; sp < spacesNum; sp++)
    fprintf(hFile, " ");
  fprintf(hFile, "%s0 = %s0 << %s;\n", name, name, shiftCountName);
}

static void generateSubMul(FILE *hFile,
                           int spacesNum,
                           const char *dvName,
                           int dvOffset,
                           const char *dsName,
                           int dsOffset,
                           int dsSize,
                           const char *mName)
{
  sp(hFile, spacesNum); fprintf(hFile, "{\n");
  sp(hFile, spacesNum+2); fprintf(hFile, "uint32_t carry0;\n");
  sp(hFile, spacesNum+2); fprintf(hFile, "uint32_t carry1;\n");
  sp(hFile, spacesNum+2); fprintf(hFile, "uint32_t lo;\n");
  sp(hFile, spacesNum+2); fprintf(hFile, "uint32_t hi;\n");
  sp(hFile, spacesNum+2); fprintf(hFile, "lo = %s%i*%s;\n", dsName, dsOffset, mName);
  sp(hFile, spacesNum+2); fprintf(hFile, "carry0 = %s%i < lo;\n", dvName, dvOffset);
  sp(hFile, spacesNum+2); fprintf(hFile, "%s%i -= lo;\n", dvName, dvOffset);
  for (int i = 0; i < dsSize-1; i++) {
    sp(hFile, spacesNum+2); fprintf(hFile, "hi = mul_hi(%s%i, %s);\n", dsName, dsOffset+i, mName);
    sp(hFile, spacesNum+2); fprintf(hFile, "lo = %s%i * %s;\n", dsName, dsOffset+i+1, mName);
    sp(hFile, spacesNum+2); fprintf(hFile, "carry1 = %s%i < hi; %s%i -= hi;\n", dvName, dvOffset+i+1, dvName, dvOffset+i+1);
    sp(hFile, spacesNum+2); fprintf(hFile, "carry1 += %s%i < lo; %s%i -= lo;\n", dvName, dvOffset+i+1, dvName, dvOffset+i+1);
    sp(hFile, spacesNum+2); fprintf(hFile, "carry1 += %s%i < carry0; %s%i -= carry0;\n", dvName, dvOffset+i+1, dvName, dvOffset+i+1);
    sp(hFile, spacesNum+2); fprintf(hFile, "carry0 = carry1;\n");
  }
  sp(hFile, spacesNum+2); fprintf(hFile, "hi = mul_hi(%s%i, %s);\n", dsName, dsSize-1, mName);
  sp(hFile, spacesNum+2); fprintf(hFile, "%s%i -= hi;\n", dvName, dvOffset+dsSize);
  sp(hFile, spacesNum+2); fprintf(hFile, "%s%i -= carry0;\n", dvName, dvOffset+dsSize);
  sp(hFile, spacesNum); fprintf(hFile, "}\n");

}

static void generateAdd(FILE *hFile,
                        int spacesNum,
                        const char *dvName,
                        int dvIndex,
                        const char *dsName,
                        int dsIndex,
                        int size)
{
  sp(hFile, spacesNum); fprintf(hFile, "%s%i += %s%i;\n", dvName, dvIndex, dsName, dsIndex);
  for (int i = 1; i < size; i++) {
    sp(hFile, spacesNum); fprintf(hFile, "%s%i += %s%i + (%s%i < %s%i);\n", dvName, dvIndex+i, dsName, dsIndex+i, dvName, dvIndex+i-1, dsName, dsIndex+i-1);
  }
}


void emitSqrProductScanGeneric(FILE *hFile, GenericCodegenTargetType target, int opSize)
{
  fprintf(hFile, "void sqrProductScan%i(uint32_t *out, uint32_t *op)\n", opSize*32);
  fprintf(hFile, "{\n");
  fprintf(hFile, "  uint64_t accLow = 0; uint64_t accHi = 0;\n");
  fprintf(hFile, "  union {\n"
         "    uint2 v32;\n"
         "    ulong v64;\n"
         "  } Int;\n");
  fprintf(hFile, "  uint32_t lo;\n");
  fprintf(hFile, "  uint32_t hi;\n\n");

  for (int i = 0; i < 2*opSize-1; i++) {
    fprintf(hFile, "  {\n");
    int off1 = std::max(i - opSize + 1, 0);
    int off2 = i - off1;
    int count = opSize - abs(opSize - i - 1);

    for (int j = 0; j < count/2; j++) {
      fprintf(hFile, "    lo = op[%i]*op[%i]; accLow += lo; accLow += lo;\n", off1, off2);
      fprintf(hFile, "    hi = mul_hi(op[%i], op[%i]); accHi += hi; accHi += hi;\n", off1, off2);
      off1++;
      off2--;
    }

    if (count & 1) {
      fprintf(hFile, "    lo = op[%i]*op[%i]; accLow += lo;\n", off1, off2);
      fprintf(hFile, "    hi = mul_hi(op[%i], op[%i]); accHi += hi;\n", off1, off2);
    }

    fprintf(hFile, "    out[%i] = accLow;\n", i);
    fprintf(hFile, "    Int.v64 = accLow;\n");
    fprintf(hFile, "    accHi += Int.v32.y;\n");
    fprintf(hFile, "    accLow = accHi;\n");
    fprintf(hFile, "    accHi = 0;\n");
    fprintf(hFile, "  }\n");
  }

  fprintf(hFile, "  out[%i] = accLow;\n", 2*opSize-1);
  fprintf(hFile, "}\n");
}

void emitMulProductScanGeneric(FILE *hFile, GenericCodegenTargetType target, int op1Size, int op2Size)
{
  fprintf(hFile, "void mulProductScan%ito%i(uint32_t *out, const uint32_t *op1, const uint32_t *op2)\n", op1Size*32, op2Size*32);
  fprintf(hFile, "{\n");
  fprintf(hFile, "  uint64_t accLow = 0;\n");
  fprintf(hFile, "  uint64_t accHi = 0;\n");
  fprintf(hFile, "  union {\n"
  "    uint2 v32;\n"
  "    ulong v64;\n"
  "  } Int;\n");
  fprintf(hFile, "  uint32_t lo;\n");
  fprintf(hFile, "  uint32_t hi;\n");

  for (int i = 0; i < op1Size+op2Size-1; i++) {
    fprintf(hFile, "  {\n");
    int off1 = std::max(i - op2Size + 1, 0);
    int off2 = i - off1;
    int count = std::min(op1Size-off1, i-off1+1);
    for (int j = 0; j < count; j++, off1++, off2--) {
      fprintf(hFile, "    lo = op1[%i]*op2[%i]; accLow += lo;\n", off1, off2);
      fprintf(hFile, "    hi = mul_hi(op1[%i], op2[%i]); accHi += hi;\n", off1, off2);
    }

    fprintf(hFile, "    out[%i] = accLow;\n", i);
    fprintf(hFile, "    Int.v64 = accLow;\n");
    fprintf(hFile, "    accHi += Int.v32.y;\n");
    fprintf(hFile, "    accLow = accHi;\n");
    fprintf(hFile, "    accHi = 0;\n");

    fprintf(hFile, "  }\n");
  }

  fprintf(hFile, "  out[%i] = accLow;\n", op1Size+op2Size-1);
  fprintf(hFile, "}\n\n");
}

void emitMulProductScanToSingleGeneric(FILE *hFile, GenericCodegenTargetType target, int op1Size)
{
  fprintf(hFile, "void mulProductScan%ito32(uint32_t *out, uint32_t *op1, uint32_t M)\n", op1Size*32);
  fprintf(hFile, "{\n");
  fprintf(hFile, "  uint64_t accLow = 0, accHi = 0;\n");
  fprintf(hFile, "  union {\n"
  "    uint2 v32;\n"
  "    ulong v64;\n"
  "  } Int;\n");
  fprintf(hFile, "  uint32_t lo0;\n");
  fprintf(hFile, "  uint32_t hi0;\n");
  constexpr int regNumX = 2;

  for (int i = 0; i < op1Size+1-1; i++) {
    fprintf(hFile, "  {\n");
    int off1 = std::max(i - 1 + 1, 0);
    int off2 = i - off1;
    int count = std::min(op1Size-off1, i-off1+1);

    int regNum = regNumX;
    if (count == regNum + 1)
      regNum++;
    for (int j = 0; j < count; j += regNum, off1 += regNum, off2 -= regNum) {
      if (count-j == regNum + 1)
        regNum++;

      for (int k = 0; k < std::min(count-j, regNum); k++) {
        fprintf(hFile, "    lo%i = op1[%i]*M;\n", k, off1+k);
        fprintf(hFile, "    hi%i = mul_hi(op1[%i], M);\n", k, off1+k);
      }

      for (int k = 0; k < std::min(count-j, regNum); k++) {
        fprintf(hFile, "    accLow += lo%i;\n", k);
        fprintf(hFile, "    accHi += hi%i;\n", k);
      }
    }

    fprintf(hFile, "    out[%i] = accLow;\n", i);
    fprintf(hFile, "    Int.v64 = accLow;\n");
    fprintf(hFile, "    accHi += Int.v32.y;\n");
    fprintf(hFile, "    accLow = accHi;\n");
    fprintf(hFile, "    accHi = 0;\n");

    fprintf(hFile, "  }\n");
  }

  fprintf(hFile, "  out[%i] = accLow;\n", op1Size+1-1);
  fprintf(hFile, "}\n\n");
}

void emitMontgomerySqrGeneric(FILE *hFile, GenericCodegenTargetType target, int opSize)
{
  fprintf(hFile, "void monSqr%i(uint32_t *op, const uint32_t *mod, uint32_t invm)\n", opSize*32);
  fprintf(hFile, "{\n");
  fprintf(hFile, "  uint32_t invValue[%i];\n", opSize);
  fprintf(hFile, "  uint64_t accLow = 0, accHi = 0;\n");
  fprintf(hFile, "  union {\n"
  "    uint2 v32;\n"
  "    ulong v64;\n"
  "  } Int;\n");

  for (int i = 0; i < 2*opSize-1; i++) {
    fprintf(hFile, "  {\n");
    int off1 = std::max(i - opSize + 1, 0);
    int off2 = i - off1;
    int count = opSize - abs(opSize - i - 1);

    for (int j = 0; j < count/2; j++) {
      fprintf(hFile, "    uint32_t low%i = op[%i]*op[%i];\n", j, off1, off2);
      fprintf(hFile, "    uint32_t hi%i = mul_hi(op[%i], op[%i]);\n", j, off1, off2);
      off1++;
      off2--;
    }

    for (int j = 0; j < count/2; j++) {
      fprintf(hFile, "    accLow += low%i; accLow += low%i;\n", j, j);
      fprintf(hFile, "    accHi += hi%i; accHi += hi%i;\n", j, j);
    }

    if (count & 1) {
      fprintf(hFile, "    accLow += op[%i]*op[%i];\n", off1, off2);
      fprintf(hFile, "    accHi += mul_hi(op[%i], op[%i]);\n", off1, off2);
    }

    off1 = std::max(i - opSize + 1, 0);
    off2 = i - off1;
    for (int j = 0; j < count; j++) {
      if (i < opSize && off1 == i)
        fprintf(hFile, "    invValue[%i] = invm * (uint32_t)accLow;\n", i);
      fprintf(hFile, "    accLow += invValue[%i]*mod[%i];\n", off1, off2);
      fprintf(hFile, "    accHi += mul_hi(invValue[%i], mod[%i]);\n", off1, off2);
      off1++;
      off2--;
    }

    if (i >= opSize)
      fprintf(hFile, "    op[%i] = accLow;\n", i-opSize);
    fprintf(hFile, "    Int.v64 = accLow;\n");
    fprintf(hFile, "    accHi += Int.v32.y;\n");
    fprintf(hFile, "    accLow = accHi;\n");
    fprintf(hFile, "    accHi = 0;\n");
    fprintf(hFile, "  }\n");
  }

  fprintf(hFile, "  op[%i] = accLow;\n", opSize-1);

  fprintf(hFile, "  Int.v64 = accLow;\n");
  fprintf(hFile, "  if (Int.v32.y)\n");
  fprintf(hFile, "    sub(op, mod, %i);\n", opSize);
  fprintf(hFile, "}\n\n");
}

void emitMontgomeryMulGeneric(FILE *hFile, GenericCodegenTargetType target, int opSize)
{
  fprintf(hFile, "void monMul%i(uint32_t *op1, const uint32_t *op2, const uint32_t *mod, uint32_t invm)\n", opSize*32);
  fprintf(hFile, "{\n");
  fprintf(hFile, "  uint32_t invValue[%i];\n", opSize);
  fprintf(hFile, "  uint64_t accLow = 0, accHi = 0;\n");
  fprintf(hFile, "  union {\n"
  "    uint2 v32;\n"
  "    ulong v64;\n"
  "  } Int;\n");

  for (int i = 0; i < 2*opSize-1; i++) {
    fprintf(hFile, "  {\n");
    int off1 = std::max(i - opSize + 1, 0);
    int off2 = i - off1;
    int count = opSize - abs(opSize - i - 1);

    for (int j = 0; j < count; j++, off1++, off2--) {
      fprintf(hFile, "    accLow += op1[%i]*op2[%i];\n", off1, off2);
      fprintf(hFile, "    accHi += mul_hi(op1[%i], op2[%i]);\n", off1, off2);
    }

    off1 = std::max(i - opSize + 1, 0);
    off2 = i - off1;
    for (int j = 0; j < count; j++) {
      if (i < opSize && off1 == i)
        fprintf(hFile, "    invValue[%i] = invm * (uint32_t)accLow;\n", i);
      fprintf(hFile, "    accLow += invValue[%i]*mod[%i];\n", off1, off2);
      fprintf(hFile, "    accHi += mul_hi(invValue[%i], mod[%i]);\n", off1, off2);
      off1++;
      off2--;
    }

    if (i >= opSize)
      fprintf(hFile, "    op1[%i] = accLow;\n", i-opSize);
    fprintf(hFile, "    Int.v64 = accLow;\n");
    fprintf(hFile, "    accHi += Int.v32.y;\n");
    fprintf(hFile, "    accLow = accHi;\n");
    fprintf(hFile, "    accHi = 0;\n");
    fprintf(hFile, "  }\n");
  }

  fprintf(hFile, "  op1[%i] = accLow;\n", opSize-1);

  fprintf(hFile, "  Int.v64 = accLow;\n");
  fprintf(hFile, "  if (Int.v32.y)\n");
  fprintf(hFile, "    sub(op1, mod, %i);\n", opSize);
  fprintf(hFile, "}\n\n");
}

void emitRedcHalfGeneric(FILE *hFile, GenericCodegenTargetType target, int opSize)
{
  fprintf(hFile, "void redcHalf%i(uint32_t *op, const uint32_t *mod, uint32_t invm)\n", opSize*32);
  fprintf(hFile, "{\n");
  fprintf(hFile, "  uint32_t invValue[%i];\n", opSize);
  fprintf(hFile, "  uint64_t accLow = op[0], accHi = op[1];\n");
  fprintf(hFile, "  union {\n"
  "    uint2 v32;\n"
  "    ulong v64;\n"
  "  } Int;\n");

  for (int i = 0; i < 2*opSize-1; i++) {
    fprintf(hFile, "  {\n");
    int off1 = std::max(i - opSize + 1, 0);
    int off2 = i - off1;
    int count = opSize - abs(opSize - i - 1);

    off1 = std::max(i - opSize + 1, 0);
    off2 = i - off1;
    for (int j = 0; j < count; j++) {
      if (i < opSize && off1 == i)
        fprintf(hFile, "    invValue[%i] = invm * (uint32_t)accLow;\n", i);
      fprintf(hFile, "    accLow += invValue[%i]*mod[%i];\n", off1, off2);
      fprintf(hFile, "    accHi += mul_hi(invValue[%i], mod[%i]);\n", off1, off2);
      off1++;
      off2--;
    }

    if (i >= opSize)
      fprintf(hFile, "    op[%i] = accLow;\n", i-opSize);
    fprintf(hFile, "    Int.v64 = accLow;\n");
    fprintf(hFile, "    accHi += Int.v32.y;\n");
    fprintf(hFile, "    accLow = accHi;\n");
    if (i+2 < opSize)
      fprintf(hFile, "    accHi = op[%i];\n", i+2);
    else
      fprintf(hFile, "    accHi = 0;\n");
    fprintf(hFile, "  }\n");
  }

  fprintf(hFile, "  op[%i] = accLow;\n", opSize-1);

  fprintf(hFile, "  Int.v64 = accLow;\n");
  fprintf(hFile, "  if (Int.v32.y)\n");
  fprintf(hFile, "    sub(op, mod, %i);\n", opSize);
  fprintf(hFile, "}\n\n");
}

void emitGenerateDivRegCGeneric(FILE *hFile, GenericCodegenTargetType target, int dividendSize, int divisorSize, int qSize)
{
  // Prototype
  fprintf(hFile, "uint32_t divide%ito%ireg(", dividendSize*32, divisorSize*32);
  for (int i = 0; i < dividendSize; i++) {
    if (i != 0)
      fprintf(hFile, ", ");
    fprintf(hFile, "uint32_t dv%i", i);
  }
  fprintf(hFile, ",\n    ");
 for (int i = 0; i < divisorSize; i++) {
    if (i != 0)
      fprintf(hFile, ", ");
    fprintf(hFile, "uint32_t ds%i", i);
  }
  fprintf(hFile, ",\n    ");
 for (int i = 0; i < qSize; i++) {
    if (i != 0)
      fprintf(hFile, ", ");
    fprintf(hFile, "uint32_t *q%i", i);
  }
  fprintf(hFile, ")\n{\n");

  // Variables
  fprintf(hFile, "  unsigned dividendSize = %i;\n", dividendSize);
  fprintf(hFile, "  unsigned divisorSize = %i;\n", divisorSize);

  // NormalizeDivisor
  fprintf(hFile, "  while (ds%i == 0) {\n", divisorSize-1);
  generateShl32(hFile, 4, "ds", divisorSize);
  fprintf(hFile, "    divisorSize--;\n");
  fprintf(hFile, "  }\n\n");

  fprintf(hFile, "  unsigned shiftCount = clz(ds%i);\n", divisorSize-1);
  fprintf(hFile, "  if (shiftCount) {\n");
  generateShl(hFile, 4, "ds", "shiftCount", divisorSize);
  generateShl(hFile, 4, "dv", "shiftCount", dividendSize);
  fprintf(hFile, "  }\n\n");

  fprintf(hFile, "  while (dv%i == 0) {\n", dividendSize-1);
  generateShl32(hFile, 4, "dv", dividendSize);
  fprintf(hFile, "    dividendSize--;\n");
  fprintf(hFile, "  }\n\n");

  fprintf(hFile, "  unsigned cyclesNum = min(dividendSize-divisorSize, %iu);\n", qSize);
  fprintf(hFile, "  for (unsigned i = 0; i < cyclesNum; i++) {\n");
  fprintf(hFile, "    uint32_t i32quotient = 0;\n");
  fprintf(hFile, "    if (dv%i == ds%i) {\n", dividendSize-1, divisorSize-1);
  fprintf(hFile, "      i32quotient = 0xFFFFFFFF;\n");
  fprintf(hFile, "    } else {\n");
  fprintf(hFile, "      uint64_t i64dividend = (((uint64_t)dv%i) << 32) | dv%i;\n", dividendSize-1, dividendSize-2);
  fprintf(hFile, "      i32quotient = i64dividend / ds%i;\n", divisorSize-1);
  fprintf(hFile, "    }\n\n");

  generateSubMul(hFile, 4, "dv", dividendSize-divisorSize-1, "ds", 0, divisorSize, "i32quotient");
  fprintf(hFile, "\n");

  fprintf(hFile, "    uint32_t borrow = dv%i;\n", dividendSize-1);
  generateShl32(hFile, 4, "dv", dividendSize);
  fprintf(hFile, "    if (borrow) {\n");
  fprintf(hFile, "      i32quotient--;\n");
  generateAdd(hFile, 6, "dv", dividendSize-divisorSize, "ds", 0, divisorSize);
  fprintf(hFile, "      if (dv%i > ds%i) {\n", dividendSize-1, divisorSize-1);
  fprintf(hFile, "        i32quotient--;\n");
  generateAdd(hFile, 8, "dv", dividendSize-divisorSize, "ds", 0, divisorSize);
  fprintf(hFile, "      }\n");
  fprintf(hFile, "    }\n");

  generateShl32(hFile, 4, "*q", qSize, "i32quotient");

  fprintf(hFile, "  }\n\n");

  fprintf(hFile, "  return %i - 32*(%i-divisorSize) - shiftCount;\n", divisorSize*32, divisorSize);
  fprintf(hFile, "}\n\n");
}

