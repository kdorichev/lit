#include <stdlib.h>
#include <stdio.h>
#include <memory.h>

#include "lit.h"
#include "lit_parser.h"
#include "lit_debug.h"
#include "lit_memory.h"

static LitExpression* parse_expression(LitLexer* lexer);
static LitStatement* parse_statement(LitLexer* lexer);
static LitStatement* parse_declaration(LitLexer* lexer);
static LitStatement* parse_var_declaration(LitLexer* lexer);

static LitToken* copy_token(LitLexer* lexer, LitToken* old) {
	LitToken* token = (LitToken*) reallocate(lexer->vm, NULL, 0, sizeof(LitToken));

	token->start = old->start;
	token->length = old->length;
	token->line = old->line;
	token->type = old->type;

	return token;
}

static const char* copy_string(LitLexer* lexer, LitToken* name) {
	char* str = (char*) reallocate(lexer->vm, NULL, 0, name->length + 1);
	strncpy(str, name->start, name->length);
	str[name->length] = '\0';

	return str;
}

static const char* copy_string_native(LitLexer* lexer, const char* name, int length) {
	char* str = (char*) reallocate(lexer->vm, NULL, 0, length + 1);
	strncpy(str, name, length);
	str[length] = '\0';

	return str;
}

static LitToken advance(LitLexer* lexer) {
	lexer->previous = lexer->current;
	lexer->current = lit_lexer_next_token(lexer);

	return lexer->current;
}

static inline bool is_at_end(LitLexer* lexer) {
	return lexer->current.type == TOKEN_EOF;
}

static bool match(LitLexer* lexer, LitTokenType type) {
	if (lexer->current.type == type) {
		advance(lexer);
		return true;
	}

	return false;
}

static void synchronize(LitLexer* lexer) {
	advance(lexer);

	while (!is_at_end(lexer)) {
		switch (lexer->current.type) {
			case TOKEN_CLASS:
			case TOKEN_FUN:
			case TOKEN_VAR:
			case TOKEN_FOR:
			case TOKEN_IF:
			case TOKEN_WHILE:
			case TOKEN_SWITCH:
			case TOKEN_RETURN:
				return;
		}
	}

	advance(lexer);
}

static void error(LitLexer* lexer, LitToken *token, const char* message) {
	if (lexer->panic_mode) {
		return;
	}

	lexer->panic_mode = true;
	lexer->had_error = true;

	printf("[line %d] Error", token->line);

	if (token->type == TOKEN_EOF) {
		printf(" at end");
	} else if (token->type != TOKEN_ERROR) {
		printf(" at '%.*s'", token->length, token->start);
	}

	printf(": %s\n", message);
}

static LitToken consume(LitLexer* lexer, LitTokenType type, const char* message) {
	if (!match(lexer, type)) {
		error(lexer, &lexer->current, message);
		return lexer->current;
	}

	return lexer->previous;
}

static LitExpression* parse_primary(LitLexer* lexer) {
	if (match(lexer, TOKEN_IDENTIFIER)) {
		return (LitExpression*) lit_make_var_expression(lexer->vm, copy_string(lexer, &lexer->previous));
	}

	if (match(lexer, TOKEN_TRUE)) {
		return (LitExpression*) lit_make_literal_expression(lexer->vm, TRUE_VALUE);
	}

	if (match(lexer, TOKEN_FALSE)) {
		return (LitExpression*) lit_make_literal_expression(lexer->vm, FALSE_VALUE);
	}

	if (match(lexer, TOKEN_NIL)) {
		return (LitExpression*) lit_make_literal_expression(lexer->vm, NIL_VALUE);
	}

	if (match(lexer, TOKEN_NUMBER)) {
		return (LitExpression*) lit_make_literal_expression(lexer->vm, MAKE_NUMBER_VALUE(strtod(lexer->previous.start, NULL)));
	}

	if (match(lexer, TOKEN_STRING)) {
		return (LitExpression*) lit_make_literal_expression(lexer->vm, MAKE_OBJECT_VALUE(lit_copy_string(lexer->vm, lexer->previous.start + 1, lexer->previous.length - 2)));
	}

	if (match(lexer, TOKEN_LEFT_PAREN)) {
		LitExpression* expression = parse_expression(lexer);
		consume(lexer, TOKEN_RIGHT_PAREN, "Expected ')' after expression");
		return (LitExpression*) lit_make_grouping_expression(lexer->vm, expression);
	}

	error(lexer, &lexer->current, "Unexpected token");
}

static LitExpression* finish_call(LitLexer* lexer, LitExpression* callee) {
	LitExpressions* args = (LitExpressions*) reallocate(lexer->vm, NULL, 0, sizeof(LitExpressions));
	lit_init_expressions(args);

	if (lexer->current.type != TOKEN_RIGHT_PAREN) {
		do {
			lit_expressions_write(lexer->vm, args, parse_expression(lexer));
		} while (match(lexer, TOKEN_COMMA));
	}

	consume(lexer, TOKEN_RIGHT_PAREN, "Expected ')' after arguments");
	return (LitExpression*) lit_make_call_expression(lexer->vm, callee, args);
}

static LitExpression* parse_call(LitLexer* lexer) {
	LitExpression* expression = parse_primary(lexer);

	while (match(lexer, TOKEN_LEFT_PAREN)) {
		expression = finish_call(lexer, expression);
	}

	return expression;
}

static LitExpression* parse_unary(LitLexer* lexer) {
	if (match(lexer, TOKEN_BANG) || match(lexer, TOKEN_MINUS)) {
		LitTokenType operator = lexer->previous.type;
		LitExpression* right = parse_unary(lexer);
		return (LitExpression*) lit_make_unary_expression(lexer->vm, right, operator);
	}

	return parse_call(lexer);
}

static LitExpression* parse_multiplication(LitLexer* lexer) {
	LitExpression *expression = parse_unary(lexer);

	while (match(lexer, TOKEN_STAR) || match(lexer, TOKEN_SLASH)) {
		LitTokenType operator = lexer->previous.type;
		LitExpression *right = parse_unary(lexer);

		expression = (LitExpression*) lit_make_binary_expression(lexer->vm, expression, right, operator);
	}

	return expression;
}

static LitExpression* parse_addition(LitLexer* lexer) {
	LitExpression *expression = parse_multiplication(lexer);

	while (match(lexer, TOKEN_PLUS) || match(lexer, TOKEN_MINUS)) {
		LitTokenType operator = lexer->previous.type;
		LitExpression *right = parse_multiplication(lexer);

		expression = (LitExpression*) lit_make_binary_expression(lexer->vm, expression, right, operator);
	}

	return expression;
}

static LitExpression* parse_comprasion(LitLexer *lexer) {
	LitExpression *expression = parse_addition(lexer);

	while (match(lexer, TOKEN_LESS) || match(lexer, TOKEN_LESS_EQUAL) || match(lexer, TOKEN_GREATER) || match(lexer, TOKEN_GREATER_EQUAL)) {
		LitTokenType operator = lexer->previous.type;
		LitExpression *right = parse_addition(lexer);

		expression = (LitExpression*) lit_make_binary_expression(lexer->vm, expression, right, operator);
	}

	return expression;
}

static LitExpression* parse_equality(LitLexer* lexer) {
	LitExpression *expression = parse_comprasion(lexer);

	while (match(lexer, TOKEN_BANG_EQUAL) || match(lexer, TOKEN_EQUAL_EQUAL)) {
		LitTokenType operator = lexer->previous.type;
		LitExpression* right = parse_comprasion(lexer);

		expression = (LitExpression*) lit_make_binary_expression(lexer->vm, expression, right, operator);
	}

	return expression;
}

static LitExpression* parse_and(LitLexer* lexer) {
	LitExpression* expression = parse_equality(lexer);

	while (match(lexer, TOKEN_AND)) {
		LitTokenType operator = lexer->previous.type;
		LitExpression* right = parse_equality(lexer);

		expression = (LitExpression*) lit_make_logical_expression(lexer->vm, operator, expression, right);
	}

	return expression;
}

static LitExpression* parse_or(LitLexer* lexer) {
	LitExpression* expression = parse_and(lexer);

	while (match(lexer, TOKEN_OR)) {
		LitTokenType operator = lexer->previous.type;
		LitExpression* right = parse_and(lexer);

		expression = (LitExpression*) lit_make_logical_expression(lexer->vm, operator, expression, right);
	}

	return expression;
}

static LitExpression* parse_assigment(LitLexer* lexer) {
	LitExpression* expression = parse_or(lexer);

	if (match(lexer, TOKEN_EQUAL)) {
		LitToken equal = lexer->previous;
		LitExpression* value = parse_assigment(lexer);

		if (expression->type == VAR_EXPRESSION) {
			return (LitExpression*) lit_make_assign_expression(lexer->vm, ((LitVarExpression*) expression)->name, value);
		}

		error(lexer, &equal, "Invalid assigment target");
	}

	return expression;
}

static LitExpression* parse_expression(LitLexer* lexer) {
	return parse_assigment(lexer);
}

static LitStatement* parse_expression_statement(LitLexer* lexer) {
	LitExpressionStatement* statement = lit_make_expression_statement(lexer->vm, parse_expression(lexer));

	/*if (statement->expr->type != ASSIGN_EXPRESSION) {
		error(lexer, &lexer->previous, "Expression statement is not a valid statement\n");
	}*/

	return (LitStatement*) statement;
}

static LitStatement* parse_if_statement(LitLexer* lexer) {
	consume(lexer, TOKEN_LEFT_PAREN, "Expected '(' after if");
	LitExpression* condition = parse_expression(lexer);
	consume(lexer, TOKEN_RIGHT_PAREN, "Expected ')' after if condition");

	LitStatement* if_branch = parse_statement(lexer);
	LitStatement* else_branch = NULL;
	LitStatements* else_if_branches = NULL;
	LitExpressions* else_if_conditions = NULL;

	while (match(lexer, TOKEN_ELSE)) {
		if (match(lexer, TOKEN_IF)) {
			if (else_branch != NULL) {
				error(lexer, &lexer->current, "Else branch is already defined for this if");
			}

			if (else_if_branches == NULL) {
				else_if_conditions = (LitExpressions*) reallocate(lexer->vm, NULL, 0, sizeof(LitExpressions));
				lit_init_expressions(else_if_conditions);

				else_if_branches = (LitStatements*) reallocate(lexer->vm, NULL, 0, sizeof(LitStatements));
				lit_init_statements(else_if_branches);
			}

			consume(lexer, TOKEN_LEFT_PAREN, "Expected '(' after else if");
			lit_expressions_write(lexer->vm, else_if_conditions, parse_expression(lexer));
			consume(lexer, TOKEN_RIGHT_PAREN, "Expected ')' after else if condition");

				lit_statements_write(lexer->vm, else_if_branches, parse_statement(lexer));
		} else {
			else_branch = parse_statement(lexer);
		}
	}

	return (LitStatement*) lit_make_if_statement(lexer->vm, condition, if_branch, else_branch, else_if_branches, else_if_conditions);
}

static LitStatement* parse_while(LitLexer* lexer) {
	consume(lexer, TOKEN_LEFT_PAREN, "Expected '(' after while");
	LitExpression* condition = parse_expression(lexer);
	consume(lexer, TOKEN_RIGHT_PAREN, "Expected ')' after while condition");

	return (LitStatement*) lit_make_while_statement(lexer->vm, condition, parse_statement(lexer));
}

static LitStatement* parse_for(LitLexer* lexer) {
	consume(lexer, TOKEN_LEFT_PAREN, "Expected '(' after while");

	LitStatement* init = NULL;

	if (match(lexer, TOKEN_VAR)) {
		init = parse_var_declaration(lexer);
		consume(lexer, TOKEN_SEMICOLON, "Expected ; after for init");
	} else if (!match(lexer, TOKEN_SEMICOLON)) {
		init = parse_expression_statement(lexer);
		consume(lexer, TOKEN_SEMICOLON, "Expected ; after for init");
	}

	LitExpression* condition = NULL;

	if (lexer->current.type != TOKEN_SEMICOLON) {
		condition = parse_expression(lexer);
	}

	consume(lexer, TOKEN_SEMICOLON, "Expected ; after for condition");

	LitExpression* increment = NULL;

	if (lexer->current.type != TOKEN_SEMICOLON) {
		increment = parse_expression(lexer);
	}

	consume(lexer, TOKEN_RIGHT_PAREN, "Expected ')' after for");

	LitStatement* body = parse_statement(lexer);

	if (increment != NULL) {
		LitStatements* statements = (LitStatements*) reallocate(lexer->vm, NULL, 0, sizeof(LitStatements));
		lit_init_statements(statements);

		lit_statements_write(lexer->vm, statements, body);
		lit_statements_write(lexer->vm, statements, (LitStatement*) lit_make_expression_statement(lexer->vm, increment));

		body = (LitStatement*) lit_make_block_statement(lexer->vm, statements);
	}

	if (condition == NULL) {
		condition = (LitExpression*) lit_make_literal_expression(lexer->vm, TRUE_VALUE);
	}

	body = (LitStatement*) lit_make_while_statement(lexer->vm, condition, body);

	if (init != NULL) {
		LitStatements* statements = (LitStatements*) reallocate(lexer->vm, NULL, 0, sizeof(LitStatements));
		lit_init_statements(statements);

		lit_statements_write(lexer->vm, statements, init);
		lit_statements_write(lexer->vm, statements, body);

		body = (LitStatement*) lit_make_block_statement(lexer->vm, statements);
	}

	return body;
}

static LitStatement* parse_block_statement(LitLexer* lexer) {
	LitStatements* statements = (LitStatements*) reallocate(lexer->vm, NULL, 0, sizeof(LitStatements));
	lit_init_statements(statements);

	while (!match(lexer, TOKEN_RIGHT_BRACE)) {
		if (lexer->current.type == TOKEN_EOF) {
			error(lexer, &lexer->current, "Expected '}' to close the block");
			break;
		}

		lit_statements_write(lexer->vm, statements, parse_declaration(lexer));
	}

	return (LitStatement*) lit_make_block_statement(lexer->vm, statements);
}

static LitStatement* parse_fun_statement(LitLexer* lexer) {
	LitToken function_name = consume(lexer, TOKEN_IDENTIFIER, "Expected function name");
	consume(lexer, TOKEN_LEFT_PAREN, "Expected '(' after function name");

	LitParameters* parameters = NULL;

	if (lexer->current.type != TOKEN_RIGHT_PAREN) {
		parameters = (LitParameters*) reallocate(lexer->vm, NULL, 0, sizeof(LitParameters));
		lit_init_parameters(parameters);

		do {
			LitToken type = consume(lexer, TOKEN_IDENTIFIER, "Expected argument type");
			LitToken name = consume(lexer, TOKEN_IDENTIFIER, "Expected argument name");

			lit_parameters_write(lexer->vm, parameters, (LitParameter) {copy_string_native(lexer, name.start, name.length), copy_string_native(lexer, type.start, type.length)});
		} while (match(lexer, TOKEN_COMMA));
	}

	consume(lexer, TOKEN_RIGHT_PAREN, "Expected ')' after parameters");

	LitParameter return_type = (LitParameter) {NULL, "void"};

	if (match(lexer, TOKEN_GREATER)) {
		LitToken type = consume(lexer, TOKEN_IDENTIFIER, "Expected return type");

		return_type.type = copy_string(lexer, &type);
	}

	consume(lexer, TOKEN_LEFT_BRACE, "Expected '{' before function body");
	return (LitStatement*) lit_make_function_statement(lexer->vm, copy_string(lexer, &function_name), parameters, parse_block_statement(lexer), (LitParameter) {NULL, return_type.type});
}

static LitStatement* parse_return_statement(LitLexer* lexer) {
	return (LitStatement*) lit_make_return_statement(lexer->vm, (lexer->current.type != TOKEN_RIGHT_BRACE && lexer->current.type != TOKEN_EOF) ? parse_expression(lexer) : NULL);
}

static LitStatement* parse_statement(LitLexer* lexer) {
	if (match(lexer, TOKEN_LEFT_BRACE)) {
		return parse_block_statement(lexer);
	}

	if (match(lexer, TOKEN_IF)) {
		return parse_if_statement(lexer);
	}

	if (match(lexer, TOKEN_FUN)) {
		return parse_fun_statement(lexer);
	}

	if (match(lexer, TOKEN_RETURN)) {
		return parse_return_statement(lexer);
	}

	if (match(lexer, TOKEN_FOR)) {
		return parse_for(lexer);
	}

	if (match(lexer, TOKEN_WHILE)) {
		return parse_while(lexer);
	}

	return parse_expression_statement(lexer);
}

static LitStatement* parse_var_declaration(LitLexer* lexer) {
	LitToken name = consume(lexer, TOKEN_IDENTIFIER, "Expected variable name");
	LitExpression* init = NULL;

	if (match(lexer, TOKEN_EQUAL)) {
		init = parse_expression(lexer);
	}

	return (LitStatement*) lit_make_var_statement(lexer->vm, copy_string(lexer, &name), init);
}

static LitStatement* parse_declaration(LitLexer* lexer) {
	if (match(lexer, TOKEN_VAR)) {
		return parse_var_declaration(lexer);
	}

	return parse_statement(lexer);
}

bool lit_parse(LitVm* vm, LitStatements* statements) {
	LitLexer lexer;

	lit_init_lexer(&lexer, vm->code);
	lexer.vm = vm;
	advance(&lexer);

	while (!is_at_end(&lexer)) {
		lit_statements_write(vm, statements, parse_declaration(&lexer));
	}

	bool had_error = lexer.had_error;
	lit_free_lexer(&lexer);

	return had_error;
}