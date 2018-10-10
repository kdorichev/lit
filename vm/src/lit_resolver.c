#include <stdio.h>
#include <memory.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <zconf.h>

#include "lit_resolver.h"
#include "lit_table.h"
#include "lit_memory.h"
#include "lit_object.h"

DEFINE_ARRAY(LitScopes, LitLetals*, scopes)

static inline LitLetal nil_letal() {
	LitLetal letal;
  lit_init_letal(&letal);
  letal.nil = true;
  return letal;
}

DEFINE_TABLE(LitLetals, LitLetal, letals, nil_letal());
DEFINE_TABLE(LitTypes, char *, types, NULL)

static void resolve_statement(LitResolver* resolver, LitStatement* statement);
static void resolve_statements(LitResolver* resolver, LitStatements* statements);

static const char* resolve_expression(LitResolver* resolver, LitExpression* expression);
static void resolve_expressions(LitResolver* resolver, LitExpressions* expressions);

static void error(LitResolver* resolver, const char* format, ...) {
	va_list vargs;
	va_start(vargs, format);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, format, vargs);
	fprintf(stderr, "\n");
	va_end(vargs);

	resolver->had_error = true;
}

static void push_scope(LitResolver* resolver) {
	LitLetals* table = (LitLetals*) reallocate(resolver->vm, NULL, 0, sizeof(LitLetals));
	lit_init_letals(table);
	lit_scopes_write(resolver->vm, &resolver->scopes, table);

	resolver->depth ++;
}

static LitLetals* peek_scope(LitResolver* resolver) {
	return resolver->scopes.values[resolver->depth - 1];
}

static void pop_scope(LitResolver* resolver) {
	if (resolver->depth == 1) {
		error(resolver, "Can't pop global scope!");
		return;
	}

	resolver->scopes.count --;
	lit_free_letals(resolver->vm, resolver->scopes.values[resolver->scopes.count]);

	resolver->depth --;
}

static void resolve_type(LitResolver* resolver, const char* type) {
	if (lit_types_get(&resolver->types, lit_copy_string(resolver->vm, type, (int) strlen(type))) == NULL) {
		error(resolver, "Type %s is not defined", type);
	}
}

static void define_type(LitResolver* resolver, const char* type) {
	lit_types_set(resolver->vm, &resolver->types, lit_copy_string(resolver->vm, type, (int) strlen(type)), (char*) type);
}

void lit_init_letal(LitLetal* letal) {
	letal->type = NULL;
	letal->defined = false;
	letal->nil = false;
}

static LitLetal* resolve_local(LitResolver* resolver, const char* name) {
	LitString *str = lit_copy_string(resolver->vm, name, (int) strlen(name));

	for (int i = resolver->scopes.count - 1; i >= 0; i --) {
		LitLetal* value = lit_letals_get(resolver->scopes.values[i], str);

		if (!(value == NULL || value->nil)) {
			return value;
		}
	}

	error(resolver, "Variable %s is not defined", name);
	return NULL;
}

static void declare(LitResolver* resolver, const char* name) {
	LitLetals* scope = peek_scope(resolver);
	LitString* str = lit_copy_string(resolver->vm, name, (int) strlen(name));
	LitLetal* value = lit_letals_get(scope, str);

	if (value != NULL) {
		error(resolver, "Variable %s is already defined in current scope", name);
	}

	value = (LitLetal*) reallocate(resolver->vm, NULL, 0, sizeof(LitLetal));

	lit_init_letal(value);
	lit_letals_set(resolver->vm, scope, str, *value);
}

static void define(LitResolver* resolver, const char* name, const char* type) {
	LitLetals* scope = peek_scope(resolver);
	LitString* str = lit_copy_string(resolver->vm, name, (int) strlen(name));

	LitLetal* value = lit_letals_get(scope, str);

	if (value == NULL) {
		LitLetal *value = (LitLetal*) reallocate(resolver->vm, NULL, 0, sizeof(LitLetal));
		lit_init_letal(value);

		value->defined = true;
		value->type = type;

		lit_letals_set(resolver->vm, scope, str, *value);
	} else {
		value->defined = true;
		value->type = type;
	}
}

static void resolve_var_statement(LitResolver* resolver, LitVarStatement* statement) {
	declare(resolver, statement->name);
	char *type = "any";

	if (statement->init != NULL) {
		type = (char*) resolve_expression(resolver, statement->init);
	}

	if (strcmp(type, "void") == 0) {
		error(resolver, "Can't set variable's %s type to void", statement->name);
	} else {
		define(resolver, statement->name, type);
	}
}

static void resolve_expression_statement(LitResolver* resolver, LitExpressionStatement* statement) {
	resolve_expression(resolver, statement->expr);
}

static void resolve_if_statement(LitResolver* resolver, LitIfStatement* statement) {
	resolve_expression(resolver, statement->condition);
	resolve_statement(resolver, statement->if_branch);

	if (statement->else_if_branches != NULL) {
		resolve_expressions(resolver, statement->else_if_conditions);
		resolve_statements(resolver, statement->else_if_branches);
	}

	if (statement->else_branch != NULL) {
		resolve_statement(resolver, statement->else_branch);
	}
}

static void resolve_block_statement(LitResolver* resolver, LitBlockStatement* statement) {
	push_scope(resolver);
	resolve_statements(resolver, statement->statements);
	pop_scope(resolver);
}

static void resolve_while_statement(LitResolver* resolver, LitWhileStatement* statement) {
	resolve_expression(resolver, statement->condition);
	resolve_statement(resolver, statement->body);
}

static void resolve_function(LitResolver* resolver, LitFunctionStatement* statement) {
	push_scope(resolver);

	resolver->had_return = false;

	if (statement->parameters != NULL) {
		for (int i = 0; i < statement->parameters->count; i++) {
			LitParameter parameter = statement->parameters->values[i];
			resolve_type(resolver, parameter.type);
			define(resolver, parameter.name, parameter.type);
		}
	}

	resolve_type(resolver, statement->return_type.type);
	resolve_statement(resolver, statement->body);

	if (!resolver->had_return && strcmp(statement->return_type.type, "void") != 0) {
		error(resolver, "Missing return statement in function %s", statement->name);
	}

	pop_scope(resolver);
}

static const char* get_function_signature(LitResolver* resolver, LitFunctionStatement* statement) {
	size_t len = 11;

	if (statement->parameters != NULL) {
		int cn = statement->parameters->count;

		for (int i = 0; i < cn; i++) {
			len += strlen(statement->parameters->values[i].type) + 2; // ', '
		}
	}

	len += strlen(statement->return_type.type);
	char* type = (char*) reallocate(resolver->vm, NULL, 0, len);

	strncpy(type, "function<", 9);
	int place = 9;

	if (statement->parameters != NULL) {
		int cn = statement->parameters->count;

		for (int i = 0; i < cn; i++) {
			const char* tp = statement->parameters->values[i].type;
			size_t l = strlen(tp);

			strncpy(&type[place], tp, l);
			place += l;
			strncpy(&type[place], ", ", 2);
			place += 2; // ', '
		}
	}

	size_t l = strlen(statement->return_type.type); // minus \0
	strncpy(&type[place], statement->return_type.type, l);

	type[len - 2] = '>';
	type[len - 1] = '\0';

	return type;
}

static void resolve_function_statement(LitResolver* resolver, LitFunctionStatement* statement) {
	const char* type = get_function_signature(resolver, statement);

	LitFunctionStatement* last = resolver->function;
	resolver->function = statement;

	define(resolver, statement->name, type);
	resolve_function(resolver, statement);

	resolver->function = last;
}

static void resolve_return_statement(LitResolver* resolver, LitReturnStatement* statement) {
	const char* type = resolve_expression(resolver, statement->value);
	resolver->had_return = true;

	if (resolver->function == NULL) {
		error(resolver, "Can't return from top-level code!");
	} else if (strcmp(type, resolver->function->return_type.type) != 0) {
		error(resolver, "Return type mismatch: required %s, but got %s", resolver->function->return_type.type, type);
	}
}

static void resolve_statement(LitResolver* resolver, LitStatement* statement) {
	switch (statement->type) {
		case VAR_STATEMENT: resolve_var_statement(resolver, (LitVarStatement*) statement); break;
		case EXPRESSION_STATEMENT: resolve_expression_statement(resolver, (LitExpressionStatement*) statement); break;
		case IF_STATEMENT: resolve_if_statement(resolver, (LitIfStatement*) statement); break;
		case BLOCK_STATEMENT: resolve_block_statement(resolver, (LitBlockStatement*) statement); break;
		case WHILE_STATEMENT: resolve_while_statement(resolver, (LitWhileStatement*) statement); break;
		case FUNCTION_STATEMENT: resolve_function_statement(resolver, (LitFunctionStatement*) statement); break;
		case RETURN_STATEMENT: resolve_return_statement(resolver, (LitReturnStatement*) statement); break;
	}
}

static void resolve_statements(LitResolver* resolver, LitStatements* statements) {
	for (int i = 0; i < statements->count; i++) {
		resolve_statement(resolver, statements->values[i]);
	}
}

static const char* resolve_binary_expression(LitResolver* resolver, LitBinaryExpression* expression) {
	const char* a = resolve_expression(resolver, expression->left);
	const char* b = resolve_expression(resolver, expression->right);

	if (strcmp(a, "int") != 0 || strcmp(b, "int") != 0) {
		error(resolver, "Can't perform binary operation on %s and %s", a, b);
	}

	return a;
}

static const char* resolve_literal_expression(LitResolver* resolver, LitLiteralExpression* expression) {
	if (IS_NUMBER(expression->value)) {
		double number = AS_NUMBER(expression->value);
		double temp;

		if (modf(number, &temp) == 0) {
			return "int";
		}

		return "double";
	} else if (IS_BOOL(expression->value)) {
		return "bool";
	}

	// nil
	return "object";
}

static const char* resolve_unary_expression(LitResolver* resolver, LitUnaryExpression* expression) {
	return resolve_expression(resolver, expression->right);
}

static const char* resolve_grouping_expression(LitResolver* resolver, LitGroupingExpression* expression) {
	return resolve_expression(resolver, expression->expr);
}

static const char* resolve_var_expression(LitResolver* resolver, LitVarExpression* expression) {
	LitLetal* value = lit_letals_get(peek_scope(resolver), lit_copy_string(resolver->vm, expression->name, (int) strlen(expression->name)));

	if (value != NULL && !value->defined) {
		error(resolver, "Can't use local variable %s in it's own initializer", expression->name);
	}

	LitLetal* letal = resolve_local(resolver, expression->name);

	if (letal == NULL) {
		return "object";
	}

	return letal->type;
}

static const char* resolve_assign_expression(LitResolver* resolver, LitAssignExpression* expression) {
	resolve_expression(resolver, expression->value);
	LitLetal* letal = resolve_local(resolver, expression->name);

	if (letal == NULL) {
		return "object";
	}

	return letal->type;
}

static const char* resolve_logical_expression(LitResolver* resolver, LitLogicalExpression* expression) {
	return resolve_expression(resolver, expression->right);
}

static const char* resolve_call_expression(LitResolver* resolver, LitCallExpression* expression) {
	char* return_type = "void";

	if (expression->callee->type != VAR_EXPRESSION) {
		error(resolver, "Can't call non-variable of type %s", expression->callee->type);
	} else {
		const char* type = resolve_var_expression(resolver, (LitVarExpression*) expression->callee);
		char *arg = strtok((char*) &type[9], ", ");
		int i = 0;
		int cn = expression->args->count;

		while (arg != NULL) {
			if (i > cn) {
				error(resolver, "Not enough arguments for function %s", type);
				break;
			}

			if (i < cn) {
				const char* given_type = resolve_expression(resolver, expression->args->values[i]);

				if (strcmp(given_type, arg) != 0) {
					error(resolver, "Argument #%i type mismatch: required %s, but got %s", i + 1, arg, given_type);
				}
			} else {
				size_t len = strlen(arg);
				return_type = (char*) reallocate(resolver->vm, NULL, 0, len);
				strncpy(return_type, arg, len - 1);
				return_type[len - 1] = '\0';
			}

			arg = strtok(NULL, ", ");

			if (arg == NULL) {
				break;
			}

			i++;
		}
	}

	resolve_expressions(resolver, expression->args);
	return return_type;
}

static const char* resolve_expression(LitResolver* resolver, LitExpression* expression) {
	switch (expression->type) {
		case BINARY_EXPRESSION: return resolve_binary_expression(resolver, (LitBinaryExpression*) expression);
		case LITERAL_EXPRESSION: return resolve_literal_expression(resolver, (LitLiteralExpression*) expression);
		case UNARY_EXPRESSION: return resolve_unary_expression(resolver, (LitUnaryExpression*) expression);
		case GROUPING_EXPRESSION: return resolve_grouping_expression(resolver, (LitGroupingExpression*) expression);
		case VAR_EXPRESSION: return resolve_var_expression(resolver, (LitVarExpression*) expression);
		case ASSIGN_EXPRESSION: return resolve_assign_expression(resolver, (LitAssignExpression*) expression);
		case LOGICAL_EXPRESSION: return resolve_logical_expression(resolver, (LitLogicalExpression*) expression);
		case CALL_EXPRESSION: return resolve_call_expression(resolver, (LitCallExpression*) expression);
	}
}

static void resolve_expressions(LitResolver* resolver, LitExpressions* expressions) {
	for (int i = 0; i < expressions->count; i++) {
		resolve_expression(resolver, expressions->values[i]);
	}
}

void lit_init_resolver(LitResolver* resolver) {
	lit_init_scopes(&resolver->scopes);
	lit_init_types(&resolver->types);

	resolver->had_error = false;
	resolver->depth = 0;
	resolver->function = NULL;

	push_scope(resolver); // Global scope

	define_type(resolver, "int");
	define_type(resolver, "bool");
	define_type(resolver, "object");
}

void lit_free_resolver(LitResolver* resolver) {
	lit_free_scopes(resolver->vm, &resolver->scopes);
	lit_free_types(resolver->vm, &resolver->types);
}

bool lit_resolve(LitVm* vm, LitStatements* statements) {
	LitResolver resolver;
	resolver.vm = vm;

	lit_init_resolver(&resolver);
	resolve_statements(&resolver, statements);
	lit_free_resolver(&resolver);

	return resolver.had_error;
}