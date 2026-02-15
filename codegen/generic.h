#include <stdio.h>

enum GenericCodegenTargetType {
    gctOpenCL = 0,
    gctCUDA
};

void emitSqrProductScanGeneric(FILE *hFile, GenericCodegenTargetType target, int opSize);
void emitMulProductScanGeneric(FILE *hFile, GenericCodegenTargetType target, int op1Size, int op2Size);
void emitMulProductScanToSingleGeneric(FILE *hFile, GenericCodegenTargetType target, int op1Size);
void emitMontgomerySqrGeneric(FILE *hFile, GenericCodegenTargetType target, int opSize);
void emitMontgomeryMulGeneric(FILE *hFile, GenericCodegenTargetType target, int opSize);
void emitRedcHalfGeneric(FILE *hFile, GenericCodegenTargetType target, int opSize);
void emitGenerateDivRegCGeneric(FILE *hFile, GenericCodegenTargetType target, int dividendSize, int divisorSize, int qSize);
