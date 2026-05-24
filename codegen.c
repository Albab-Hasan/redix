#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"
#include "parser.h"

static FILE *out;
static int label_count;
static char loop_break_label[64];
static char loop_cont_label[64];

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

	for (i = var_count - 1; i >= 0; i--) {
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

static void gen_expression(struct ast_node *node);
static void gen_statement(struct ast_node *node);

static void gen_number(struct ast_node *node)
{
	emit("\tmov $%s, %%eax", node->value);
}

static void gen_unary(struct ast_node *node)
{
	gen_expression(node->children[0]);
	switch (node->value[0]) {
	case '-':
		emit("\tneg %%eax");
		break;
	case '~':
		emit("\tnot %%eax");
		break;
	case '!':
		emit("\tcmpl $0, %%eax");
		emit("\tmovl $0, %%eax");
		emit("\tsete %%al");
		break;
	}
}

/* arithmetic ops -- ecx holds left eax holds right */
static void gen_arith(const char *op)
{
	if (strcmp(op, "+") == 0) {
		emit("\taddl %%ecx, %%eax");
	} else if (strcmp(op, "-") == 0) {
		emit("\tsubl %%eax, %%ecx");
		emit("\tmovl %%ecx, %%eax");
	} else if (strcmp(op, "*") == 0) {
		emit("\timull %%ecx, %%eax");
	} else if (strcmp(op, "/") == 0) {
		emit("\tmovl %%eax, %%ebx");
		emit("\tmovl %%ecx, %%eax");
		emit("\tcdq");
		emit("\tidivl %%ebx");
	}
}

/* comparison ops -- ecx holds left eax holds right
 * we compare left - right and set al based on the result */
static void gen_compare(const char *op)
{
	emit("\tcmpl %%eax, %%ecx");
	emit("\tmovl $0, %%eax");
	if (strcmp(op, "<") == 0)       emit("\tsetl %%al");
	else if (strcmp(op, "<=") == 0) emit("\tsetle %%al");
	else if (strcmp(op, ">") == 0)  emit("\tsetg %%al");
	else if (strcmp(op, ">=") == 0) emit("\tsetge %%al");
	else if (strcmp(op, "==") == 0) emit("\tsete %%al");
	else if (strcmp(op, "!=") == 0) emit("\tsetne %%al");
}

/* logical ops -- normalize both sides to 0 or 1 then bitwise and/or */
static void gen_logical(const char *op)
{
	emit("\tcmpl $0, %%ecx");
	emit("\tsetne %%cl");
	emit("\tcmpl $0, %%eax");
	emit("\tsetne %%al");
	if (op[0] == '&')
		emit("\tandb %%cl, %%al");
	else
		emit("\torb %%cl, %%al");
	emit("\tmovzbl %%al, %%eax");
}

static int is_arith_op(const char *op)
{
	return op[1] == '\0' && (op[0] == '+' || op[0] == '-'
			|| op[0] == '*' || op[0] == '/');
}

static int is_compare_op(const char *op)
{
	char c = op[0];
	return c == '<' || c == '>' || c == '=' || c == '!';
}

static void gen_binary(struct ast_node *node)
{
	const char *op = node->value;

	gen_expression(node->children[0]);
	emit("\tpush %%rax");
	gen_expression(node->children[1]);
	emit("\tpop %%rcx");

	if (is_arith_op(op))
		gen_arith(op);
	else if (is_compare_op(op))
		gen_compare(op);
	else
		gen_logical(op); /* && or || */
}

static void gen_var(struct ast_node *node)
{
	int offset = find_var(node->value);

	emit("\tmovl %d(%%rbp), %%eax", offset);
}

static void gen_assign(struct ast_node *node)
{
	int offset = find_var(node->value);

	gen_expression(node->children[0]);
	emit("\tmovl %%eax, %d(%%rbp)", offset);
}

/* prefix: increment/decrement first, result is the new value */
static void gen_prefix_inc(struct ast_node *node)
{
	int offset = find_var(node->value);

	emit("\taddl $1, %d(%%rbp)", offset);
	emit("\tmovl %d(%%rbp), %%eax", offset);
}

static void gen_prefix_dec(struct ast_node *node)
{
	int offset = find_var(node->value);

	emit("\tsubl $1, %d(%%rbp)", offset);
	emit("\tmovl %d(%%rbp), %%eax", offset);
}

/* postfix: load old value into eax first, then mutate memory */
static void gen_postfix_inc(struct ast_node *node)
{
	int offset = find_var(node->value);

	emit("\tmovl %d(%%rbp), %%eax", offset);
	emit("\taddl $1, %d(%%rbp)", offset);
}

static void gen_postfix_dec(struct ast_node *node)
{
	int offset = find_var(node->value);

	emit("\tmovl %d(%%rbp), %%eax", offset);
	emit("\tsubl $1, %d(%%rbp)", offset);
}

static void gen_call(struct ast_node *node)
{
	static const char *arg_regs[] = {
		"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
	};
	int i;
	int nargs = node->child_count;

	/* evaluate args left-to-right and push each onto the stack */
	for (i = 0; i < nargs; i++) {
		gen_expression(node->children[i]);
		emit("\tpush %%rax");
	}
	/* pop right-to-left so arg0 ends up in rdi */
	for (i = nargs - 1; i >= 0; i--)
		emit("\tpop %s", arg_regs[i]);
	emit("\tcall %s", node->value);
}

static void gen_expression(struct ast_node *node)
{
	switch (node->type) {
	case NODE_NUMBER:		gen_number(node);		break;
	case NODE_UNARY:		gen_unary(node);		break;
	case NODE_BINARY:		gen_binary(node);		break;
	case NODE_VAR:			gen_var(node);			break;
	case NODE_ASSIGN:		gen_assign(node);		break;
	case NODE_CALL:			gen_call(node);			break;
	case NODE_PREFIX_INC:		gen_prefix_inc(node);		break;
	case NODE_PREFIX_DEC:		gen_prefix_dec(node);		break;
	case NODE_POSTFIX_INC:		gen_postfix_inc(node);		break;
	case NODE_POSTFIX_DEC:		gen_postfix_dec(node);		break;
	default:
		fprintf(stderr, "codegen: bad expression node type %d\n",
				node->type);
		exit(1);
	}
}

static void gen_return(struct ast_node *node)
{
	/* bare return in void functions has no expression child */
	if (node->child_count > 0)
		gen_expression(node->children[0]);
	emit("\tmovq %%rbp, %%rsp");
	emit("\tpopq %%rbp");
	emit("\tret");
}

static void gen_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value);

	if (node->child_count > 0) {
		/* has initializer */
		gen_expression(node->children[0]);
		emit("\tmovl %%eax, %d(%%rbp)", offset);
	}
}

static void gen_compound(struct ast_node *node)
{
	int i;

	for (i = 0; i < node->child_count; i++)
		gen_statement(node->children[i]);
}

static void gen_if(struct ast_node *node)
{
	int lbl = label_count++;

	gen_expression(node->children[0]);
	emit("\tcmpl $0, %%eax");
	if (node->child_count == 3) {
		emit("\tje .Lelse%d", lbl);
		gen_statement(node->children[1]);
		emit("\tjmp .Lend%d", lbl);
		emit(".Lelse%d:", lbl);
		gen_statement(node->children[2]);
	} else {
		emit("\tje .Lend%d", lbl);
		gen_statement(node->children[1]);
	}
	emit(".Lend%d:", lbl);
}

/* save current break/continue labels so nested loops can restore them */
static void push_loop_labels(char *old_break, char *old_cont,
		const char *new_break, const char *new_cont)
{
	strcpy(old_break, loop_break_label);
	strcpy(old_cont, loop_cont_label);
	strcpy(loop_break_label, new_break);
	strcpy(loop_cont_label, new_cont);
}

static void pop_loop_labels(const char *old_break, const char *old_cont)
{
	strcpy(loop_break_label, old_break);
	strcpy(loop_cont_label, old_cont);
}

static void gen_while(struct ast_node *node)
{
	int lbl = label_count++;
	char old_break[64];
	char old_cont[64];
	char start_label[64];
	char end_label[64];

	sprintf(start_label, ".Lwhile_start%d", lbl);
	sprintf(end_label, ".Lwhile_end%d", lbl);
	push_loop_labels(old_break, old_cont, end_label, start_label);

	emit("%s:", start_label);
	gen_expression(node->children[0]);
	emit("\tcmpl $0, %%eax");
	emit("\tje %s", end_label);
	gen_statement(node->children[1]);
	emit("\tjmp %s", start_label);
	emit("%s:", end_label);

	pop_loop_labels(old_break, old_cont);
}

static void gen_for(struct ast_node *node)
{
	int lbl = label_count++;
	char old_break[64];
	char old_cont[64];
	char cond_label[64];
	char inc_label[64];
	char end_label[64];

	sprintf(cond_label, ".Lfor_cond%d", lbl);
	sprintf(inc_label, ".Lfor_inc%d", lbl);
	sprintf(end_label, ".Lfor_end%d", lbl);
	push_loop_labels(old_break, old_cont, end_label, inc_label);

	gen_expression(node->children[0]);		/* init */
	emit("%s:", cond_label);
	gen_expression(node->children[1]);		/* condition */
	emit("\tcmpl $0, %%eax");
	emit("\tje %s", end_label);
	gen_statement(node->children[3]);		/* body */
	emit("%s:", inc_label);
	gen_expression(node->children[2]);		/* increment */
	emit("\tjmp %s", cond_label);
	emit("%s:", end_label);

	pop_loop_labels(old_break, old_cont);
}

static void gen_statement(struct ast_node *node)
{
	switch (node->type) {
	case NODE_RETURN:	gen_return(node);	break;
	case NODE_DECLARATION:	gen_declaration(node);	break;
	case NODE_COMPOUND:	gen_compound(node);	break;
	case NODE_IF:		gen_if(node);		break;
	case NODE_WHILE:	gen_while(node);	break;
	case NODE_FOR:		gen_for(node);		break;
	case NODE_BREAK:	emit("\tjmp %s", loop_break_label); break;
	case NODE_CONTINUE:	emit("\tjmp %s", loop_cont_label);  break;
	case NODE_ASSIGN:
	case NODE_VAR:
	case NODE_BINARY:
	case NODE_UNARY:
	case NODE_NUMBER:
	case NODE_CALL:
	case NODE_PREFIX_INC:
	case NODE_PREFIX_DEC:
	case NODE_POSTFIX_INC:
	case NODE_POSTFIX_DEC:
		/* expression statement */
		gen_expression(node);
		break;
	default:
		fprintf(stderr, "codegen: bad statement node type %d\n",
				node->type);
		exit(1);
	}
}

/* count declarations so the right amount of stack gets reserved */
static int count_declarations(struct ast_node *node)
{
	int i;
	int count = 0;

	if (node->type == NODE_DECLARATION)
		return 1;
	for (i = 0; i < node->child_count; i++)
		count += count_declarations(node->children[i]);
	return count;
}

static void gen_function(struct ast_node *node)
{
	static const char *param_regs[] = {
		"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"
	};
	int i;
	int num_params;
	int num_locals;
	int alloc_size;
	int offset;
	struct ast_node *body;

	/* reset variable state per function */
	var_count = 0;
	stack_offset = 0;
	loop_break_label[0] = '\0';
	loop_cont_label[0] = '\0';

	/* body is always last child preceding children are params */
	body = node->children[node->child_count - 1];
	num_params = node->child_count - 1;

	emit(".global %s", node->value);
	emit("%s:", node->value);
	emit("\tpushq %%rbp");
	emit("\tmovq %%rsp, %%rbp");

	/* reserve stack for both params and locals aligned to 16 */
	num_locals = count_declarations(body);
	alloc_size = (num_params + num_locals) * 4;
	if (alloc_size > 0) {
		if (alloc_size % 16 != 0)
			alloc_size += 16 - (alloc_size % 16);
		emit("\tsubq $%d, %%rsp", alloc_size);
	}

	/* copy params from argument registers onto the stack */
	for (i = 0; i < num_params && i < 6; i++) {
		offset = declare_var(node->children[i]->value);
		emit("\tmovl %s, %d(%%rbp)", param_regs[i], offset);
	}

	gen_statement(body);

	/* safety epilogue so void funcs that fall off the end still return
	 * if the last stmt was already a return this is just dead code */
	emit("\tmovl $0, %%eax");
	emit("\tmovq %%rbp, %%rsp");
	emit("\tpopq %%rbp");
	emit("\tret");
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
