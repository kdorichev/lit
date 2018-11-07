#ifndef LIT_COMPILER_H
#define LIT_COMPILER_H

#include <lit_mem_manager.h>

#include <compiler/lit_resolver.h>
#include <compiler/lit_lexer.h>
#include <vm/lit_object.h>
#include <vm/lit_chunk.h>
#include <vm/lit_memory.h>

struct sLitCompiler {
	LitMemManager mem_manager;

	LitResolver resolver;
	LitLexer lexer;
} sLitCompiler;

void lit_init_compiler(LitCompiler* compiler);
void lit_free_compiler(LitCompiler* compiler);

LitFunction* lit_compile(LitCompiler* compiler, const char* source_code);

#endif