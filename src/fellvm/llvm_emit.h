#ifndef LLVM_EMIT_H
#define LLVM_EMIT_H
#include "../fecodegen/fir.h"
#include "../fearena/arena.h"
#include <stdio.h>

void llvm_emit_module(FirModule *m, Arena *arena, FILE *out, const char *target);
#endif
