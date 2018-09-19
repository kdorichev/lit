#include <stdio.h>
#include <stdlib.h>

#include "lit_compiler.h"
#include "lit_vm.h"
#include "lit_debug.h"

void lit_init_compiler(LitCompiler* compiler) {

}

void lit_free_compiler(LitCompiler* compiler) {
	lit_free_lexer(&compiler->lexer);
}

/*
 * Error handling
 */

static void error_at(LitCompiler* compiler, LitToken* token, const char* message) {
	compiler->lexer.panic_mode = true;

	if (compiler->lexer.panic_mode) {
		// Skip errors till we get to normal
		return;
	}

	fprintf(stderr, "[line %d] Error", token->line);

	if (token->type == TOKEN_EOF) {
		fprintf(stderr, " at end");
	} else if (token->type != TOKEN_ERROR) {
		fprintf(stderr, " at '%.*s'", token->length, token->start);
	}

	fprintf(stderr, ": %s\n", message);
	compiler->lexer.had_error = true;
}

static inline void error(LitCompiler* compiler, const char* message) {
	error_at(compiler, &compiler->lexer.previous, message);
}

static inline void error_at_current(LitCompiler* compiler, const char* message) {
	error_at(compiler, &compiler->lexer.current, message);
}

/*
 * Utils for parsing tokens
 */

static void advance(LitCompiler* compiler) {
	compiler->lexer.previous = compiler->lexer.current;

	for (;;) {
		compiler->lexer.current = lit_lexer_next_token(&compiler->lexer);

		// printf("Token: %i\n", compiler->lexer.current.type);

		if (compiler->lexer.current.type != TOKEN_ERROR) {
			break;
		}

		error_at_current(&compiler, compiler->lexer.current.start);
	}
}

static void consume(LitCompiler* compiler, LitTokenType type, const char* message) {
	if (compiler->lexer.current.type == type) {
		advance(compiler);
		return;
	}

	error_at_current(compiler, message);
}

static inline bool check(LitCompiler* compiler, LitTokenType type) {
	return compiler->lexer.current.type == type;
}

static bool match(LitCompiler* compiler, LitTokenType type) {
	if (!check(compiler, type)) {
		return false;
	}

	advance(compiler);
	return true;
}

/*
 * Bytecode utils
 */

static inline void emit_byte(LitCompiler* compiler, uint8_t byte) {
	lit_chunk_write(compiler->vm, compiler->chunk, byte, compiler->lexer.previous.line);
}

static void emit_bytes(LitCompiler* compiler, uint8_t a, uint8_t b) {
	lit_chunk_write(compiler->vm, compiler->chunk, a, compiler->lexer.previous.line);
	lit_chunk_write(compiler->vm, compiler->chunk, b, compiler->lexer.previous.line);
}

static uint8_t make_constant(LitCompiler* compiler, LitValue value) {
	int constant = lit_chunk_add_constant(compiler->vm, compiler->chunk, value);

	if (constant > UINT8_MAX) {
		error(compiler, "Too many constants in one chunk");
		return 0;
	}

	return (uint8_t) constant;
}

static inline void emit_constant(LitCompiler* compiler, LitValue value) {
	emit_bytes(compiler, OP_CONSTANT, make_constant(compiler, value));
}

/*
 * Actuall compilation
 */

static void parse_expression(LitCompiler* compiler);
static void parse_precedence(LitCompiler* compiler, LitPrecedence precedence);

static void init_parse_rules();
static LitParseRule parse_rules[TOKEN_EOF + 1];
static bool inited_parse_rules = false;

static inline LitParseRule* get_parse_rule(LitTokenType type) {
	return &parse_rules[type];
}

static void parse_number(LitCompiler* compiler) {
	emit_constant(compiler, MAKE_NUMBER_VALUE(strtod(compiler->lexer.previous.start, NULL)));
}

static void parse_grouping(LitCompiler* compiler) {
	parse_expression(compiler);
	consume(compiler, TOKEN_RIGHT_PAREN, "Expected ')' after expression");
}

static void parse_unary(LitCompiler* compiler) {
	LitTokenType operatorType = compiler->lexer.previous.type;
	parse_precedence(compiler, PREC_UNARY);

	switch (operatorType) {
		case TOKEN_BANG: emit_byte(compiler, OP_NOT); break;
		case TOKEN_MINUS: emit_byte(compiler, OP_NEGATE); break;
		default: UNREACHABLE();
	}
}

static void parse_binary(LitCompiler* compiler) {
	LitTokenType operator_type = compiler->lexer.previous.type;

	LitParseRule* rule = get_parse_rule(operator_type);
	parse_precedence(compiler, (LitPrecedence) (rule->precedence + 1));

	switch (operator_type) {
		case TOKEN_PLUS: emit_byte(compiler, OP_ADD); break;
		case TOKEN_MINUS: emit_byte(compiler, OP_SUBTRACT); break;
		case TOKEN_STAR: emit_byte(compiler, OP_MULTIPLY); break;
		case TOKEN_SLASH: emit_byte(compiler, OP_DIVIDE); break;
		case TOKEN_EQUAL_EQUAL: emit_byte(compiler, OP_EQUAL); break;
		case TOKEN_BANG_EQUAL: emit_byte(compiler, OP_NOT_EQUAL); break;
		case TOKEN_LESS_EQUAL: emit_byte(compiler, OP_LESS_EQUAL); break;
		case TOKEN_GREATER_EQUAL: emit_byte(compiler, OP_GREATER_EQUAL); break;
		case TOKEN_LESS: emit_byte(compiler, OP_LESS); break;
		case TOKEN_GREATER: emit_byte(compiler, OP_GREATER); break;
		default: UNREACHABLE();
	}
}

static void parse_literal(LitCompiler* compiler) {
	switch (compiler->lexer.previous.type) {
		case TOKEN_NIL: emit_byte(compiler, OP_NIL); break;
		case TOKEN_TRUE: emit_byte(compiler, OP_TRUE); break;
		case TOKEN_FALSE: emit_byte(compiler, OP_FALSE); break;
		default: UNREACHABLE();
	}
}

static void parse_precedence(LitCompiler* compiler, LitPrecedence precedence) {
	advance(compiler);
	LitParseFn prefix = get_parse_rule(compiler->lexer.previous.type)->prefix;

	if (prefix == NULL) {
		error(compiler, "Expected expression");
		return;
	}

	prefix(compiler);

	while (precedence <= get_parse_rule(compiler->lexer.current.type)->precedence) {
		advance(compiler);
		get_parse_rule(compiler->lexer.previous.type)->infix(compiler);
	}
}

static void parse_expression(LitCompiler* compiler) {
	parse_precedence(compiler, PREC_ASSIGNMENT);
}

static void parse_statement(LitCompiler* compiler) {
	if (match(compiler, TOKEN_PRINT)) {
		parse_expression(compiler);
		emit_byte(compiler, OP_PRINT);
	} else {
		parse_expression(compiler);
		// emit_byte(compiler, OP_POP);
	}
}

static void parse_declaration(LitCompiler* compiler) {
	parse_statement(compiler);
}

bool lit_compile(LitCompiler* compiler, LitChunk* chunk, const char* code) {
	if (!inited_parse_rules) {
		init_parse_rules();
		inited_parse_rules = true;
	}

	compiler->lexer.vm = compiler->vm;
	compiler->chunk = chunk;

	lit_init_lexer(&compiler->lexer, code);

	advance(compiler);

	if (!match(compiler, TOKEN_EOF)) {
		do {
			parse_declaration(compiler);
		} while (!match(compiler, TOKEN_EOF));
	}

	emit_byte(compiler, OP_RETURN);

#ifdef DEBUG_PRINT_CODE
	if (!compiler->lexer.had_error) {
		lit_trace_chunk(compiler->chunk, "code");
  }
#endif

	return !compiler->lexer.had_error;
}

static void init_parse_rules() {
	parse_rules[TOKEN_LEFT_PAREN] = (LitParseRule) { parse_grouping, NULL, PREC_CALL };
	parse_rules[TOKEN_MINUS] = (LitParseRule) { parse_unary, parse_binary, PREC_TERM };
	parse_rules[TOKEN_PLUS] = (LitParseRule) { NULL, parse_binary, PREC_TERM };
	parse_rules[TOKEN_SLASH] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	parse_rules[TOKEN_STAR] = (LitParseRule) { NULL, parse_binary, PREC_FACTOR };
	parse_rules[TOKEN_NUMBER] = (LitParseRule) { parse_number, NULL, PREC_NONE };
	parse_rules[TOKEN_BANG] = (LitParseRule) { parse_unary, NULL, PREC_NONE };
	parse_rules[TOKEN_NIL] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	parse_rules[TOKEN_TRUE] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	parse_rules[TOKEN_FALSE] = (LitParseRule) { parse_literal, NULL, PREC_NONE };
	parse_rules[TOKEN_EQUAL_EQUAL] = (LitParseRule) { NULL, parse_binary, PREC_EQUALITY };
	parse_rules[TOKEN_GREATER] = (LitParseRule) { NULL, parse_binary, PREC_COMPARISON };
	parse_rules[TOKEN_LESS] = (LitParseRule) { NULL, parse_binary, PREC_COMPARISON };
	parse_rules[TOKEN_GREATER_EQUAL] = (LitParseRule) { NULL, parse_binary, PREC_COMPARISON };
	parse_rules[TOKEN_LESS_EQUAL] = (LitParseRule) { NULL, parse_binary, PREC_COMPARISON };
	parse_rules[TOKEN_BANG_EQUAL] = (LitParseRule) { NULL, parse_binary, PREC_EQUALITY };
}