#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "codegen.h"
#include "parser.h"

static FILE *out;

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
		}
	} else {
		fprintf(stderr, "codegen: unknown expression type\n");
		exit(1);
	}
}

static void gen_statement(struct ast_node *node)
{
	if (node->type == NODE_RETURN) {
		gen_expression(node->children[0]);
		emit("\tret");
	} else {
		fprintf(stderr, "codegen: unknown statement type\n");
		exit(1);
	}
}

static void gen_function(struct ast_node *node)
{
	emit(".global %s", node->value);
	emit("%s:", node->value);
	gen_statement(node->children[0]);
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
