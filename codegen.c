#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"
#include "parser.h"

static FILE *out;

/* variable stack map tracks where each local lives on the stack */
#define MAX_VARS 128
static struct {
	char *name;
	int offset; /* negative offset from rbp */
} var_map[MAX_VARS];
static int var_count;
static int stack_offset;

/* look up a variable stack offset by name */
static int find_var(const char *name)
{
	int i;
	for (i = 0; i < var_count; i++) {
		if (strcmp(var_map[i].name, name) == 0)
			return var_map[i].offset;
	}
	fprintf(stderr, "codegen: undefined variable '%s'\n", name);
	exit(1);
	return 0;
}

/* add a new variable to the stack map */
static int declare_var(const char *name)
{
	stack_offset -= 4;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_count++;
	return stack_offset;
}

/* emit a line of assembly */
static void emit(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(out, fmt, args);
	va_end(args);
	fprintf(out, "\n");
}

static void gen_expression(struct ast_node *node)
{
	if (node->type == NODE_NUMBER) {
		emit("\tmov $%s, %%eax", node->value);
	} else if (node->type == NODE_UNARY) {
		/* after generating the operand first the result goes in eax */
		gen_expression(node->children[0]);

		if (node->value[0] == '-') {
			emit("\tneg %%eax");
		} else if (node->value[0] == '~') {
			emit("\tnot %%eax");
		} else if (node->value[0] == '!') {
			emit("%tcmpl $0, %%eax");
			emit("\tmovl $0, %%eax");
			emit("\tsete %%al");
		}
	} else if (node->type == NODE_BINARY) {
		gen_expression(node->children[0]);
		emit("\tpush %%rax");

		gen_expression(node->children[1]);

		emit("\tpop %%rcx");

		if (node->value[0] == '+') {
			emit("\taddl %%ecx, %%eax");
		} else if (node->value[0] == '-') {
			emit("\tsubl %%eax, %%ecx");
			emit("\tmovl %%ecx, %%eax");
		} else if (node->value[0] == '*') {
			emit("\timull %%ecx, %%eax");
		} else if (node->value[0] == '/') {
			emit("\tmovl %%eax, %%ebx");
			emit("\tmovl %%ecx, %%eax");
			emit("\tcdq");
			emit("\tidivl %%ebx");
		} else if (node->value[0] == '<' || node->value[0] == '>' ||
				node->value[0] == '=' || node->value[0] == '!') {
			/* ecx = left, eax = right -- compare left - right */
			emit("\tcmpl %%eax, %%ecx");
			emit("\tmovl $0, %%eax");
			if (node->value[0] == '<' && node->value[1] == '=')
				emit("\tsetle %%al");
			else if (node->value[0] == '<')
				emit("\tsetl %%al");
			else if (node->value[0] == '>' && node->value[1] == '=')
				emit("\tsetge %%al");
			else if (node->value[0] == '>')
				emit("\tsetg %%al");
			else if (node->value[0] == '=')
				emit("\tsete %%al");
			else
				emit("\tsetne %%al");
		} else if (node->value[0] == '&' || node->value[0] == '|') {
			/* normalize left (ecx) and right (eax) to 0 or 1, then and/or */
			emit("\tcmpl $0, %%ecx");
			emit("\tsetne %%cl");
			emit("\tcmpl $0, %%eax");
			emit("\tsetne %%al");
			if (node->value[0] == '&')
				emit("\tandb %%cl, %%al");
			else
				emit("\torb %%cl, %%al");
			emit("\tmovzbl %%al, %%eax");
		}
	} else if (node->type == NODE_VAR) {
		int offset = find_var(node->value);
		emit("\tmovl %d(%%rbp), %%eax", offset);
	} else if (node->type == NODE_ASSIGN) {
		int offset = find_var(node->value);
		gen_expression(node->children[0]);
		emit("\tmovl %%eax, %d(%%rbp)", offset);
	} else {
		fprintf(stderr, "codegen: unknown expression type\n");
		exit(1);
	}
}

static void gen_statement(struct ast_node *node)
{
	if (node->type == NODE_RETURN) {
		gen_expression(node->children[0]);
		emit("\tmovq %%rbp, %%rsp");
		emit("\tpopq %%rbp");
		emit("\tret");
	} else if (node->type == NODE_DECLARATION) {
		int offset = declare_var(node->value);
		if (node->child_count > 0) {
			/* has initializer */
			gen_expression(node->children[0]);
			emit("\tmovl %%eax, %d(%%rbp)", offset);
		}
	} else if (node->type == NODE_ASSIGN || node->type == NODE_VAR ||
			node->type == NODE_BINARY || node->type == NODE_UNARY ||
			node->type == NODE_NUMBER) {
		/* expression statement */
		gen_expression(node);
	} else {
		fprintf(stderr, "codegen: unknown statement type\n");
		exit(1);
	}
}

/* count declarations so the right amount of stack gets reserved */
static int count_declarations(struct ast_node *body)
{
	int i;
	int count = 0;
	for (i = 0; i < body->child_count; i++) {
		if (body->children[i]->type == NODE_DECLARATION)
			count++;
	}
	return count;
}

static void gen_function(struct ast_node *node)
{
	int i;
	int num_vars;
	int alloc_size;
	struct ast_node *body;

	/* reset variable state */
	var_count = 0;
	stack_offset = 0;

	emit(".global %s", node->value);
	emit("%s:", node->value);
	emit("\tpushq %%rbp");
	emit("\tmovq %%rsp, %%rbp");

	/* reserve stack space for locals aligned to 16 */
	body = node->children[0];
	num_vars = count_declarations(body);
	if (num_vars > 0) {
		alloc_size = num_vars * 4;
		if (alloc_size % 16 != 0)
			alloc_size = alloc_size + (16 - alloc_size % 16);
		emit("\tsubq $%d, %%rsp", alloc_size);
	}

	for (i = 0; i < body->child_count; i++)
		gen_statement(body->children[i]);
}

static void gen_program(struct ast_node *node)
{
	int i;

	emit("\t.text");
	for (i = 0; i < node->child_count; i++)
		gen_function(node->children[i]);
	emit("\t.section .note.GNU-stack,\"\",@progbits");
}

void codegen(struct ast_node *ast, FILE *output)
{
	out = output;
	gen_program(ast);

}
