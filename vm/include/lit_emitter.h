#ifndef LIT_EMITTER_H
#define LIT_EMITTER_H

#include "lit_object.h"
#include "lit_ast.h"

typedef struct LitLocal {
	const char* name;
	int depth;
	bool upvalue;
} LitLocal;

typedef struct LitEmitterFunction {
	LitFunction* function;
	struct LitEmitterFunction* enclosing;

	int local_count;
	int depth;

	LitLocal locals[UINT8_COUNT];
	LitUpvalue upvalues[UINT8_COUNT];
} LitEmitterFunction;

typedef struct LitEmitter {
	LitEmitterFunction* function;
	LitVm* vm;
} LitEmitter;

LitFunction* lit_emit(LitVm* vm, LitStatements* statements);

#endif