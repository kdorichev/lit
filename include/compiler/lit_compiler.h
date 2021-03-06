#ifndef LIT_COMPILER_H
#define LIT_COMPILER_H

#include <lit_mem_manager.h>

#include <compiler/lit_resolver.h>
#include <compiler/lit_lexer.h>
#include <compiler/lit_emitter.h>

#include <vm/lit_object.h>
#include <vm/lit_chunk.h>
#include <vm/lit_memory.h>

struct sLitCompiler {
	LitMemManager mem_manager;

	LitResolver resolver;
	LitLexer lexer;
	LitEmitter emitter;
	LitString* init_string;
} sLitCompiler;

void lit_init_compiler(LitCompiler* compiler);
void lit_compiler_define_native(LitCompiler* compiler, LitNativeRegistry* native);
void lit_compiler_define_natives(LitCompiler* compiler, LitNativeRegistry* natives);
void lit_free_compiler(LitCompiler* compiler);

/*
 * Should be called after you freed the compiler
 * and you are done working with LitFunction*
 */
void lit_free_bytecode_objects(LitCompiler* compiler);
LitFunction* lit_compile(LitCompiler* compiler, const char* source_code);

#endif