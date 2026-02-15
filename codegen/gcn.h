#include <stdio.h>

enum GCNArchTy {
  gcn10 = 0,
  gcn11,
  gcn12,
  gcn14
};


void emitGCNLoad(FILE *hFile, GCNArchTy arch, int opSize);
void emitGCNStore(FILE *hFile, GCNArchTy arch, int opSize);
void emitGCNLimbShl(FILE *hFile, int opSize);
void emitGCNShl(FILE *hFile, GCNArchTy arch, int opSize);
void emitGCNSubMul(FILE *hFile, GCNArchTy arch, int opSize);
void emitGCNMul_prodscan(FILE *hFile, GCNArchTy arch, int op1Size, int op2Size, int limit = -1);
void emitGCNSqr_prodscan(FILE *hFile, GCNArchTy arch, int opSize);
void emitGCNMonsqr_prodscan(FILE *hFile, GCNArchTy arch, int opSize, bool debug = false);
void emitGCNMonmul_prodscan(FILE *hFile, GCNArchTy arch, int opSize, bool debug = false);
void emitGCNRedchalf_prodscan(FILE *hFile, GCNArchTy arch, int opSize, bool carryCheckEnabled, bool debug = false);
void emitGCNRedcify_prodscan(FILE *hFile, GCNArchTy arch, int opSize, int windowSize, bool debug = false);
void emitGCNDiv(FILE *hFile, GCNArchTy arch, int dividendSize, int divisorSize, int quotientSize, bool debug = false);
void emitGCNModPow2(FILE *hFile, GCNArchTy arch, int opSize, bool debug = false);
