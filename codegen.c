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
static struct var_entry {
	char *name;
	int offset;    /* negative offset from rbp */
	int is_ptr;
	int is_array;
	int is_struct;
	int elem_size; /* 1 for char 4 for int */
	char struct_type[64];
} var_map[MAX_VARS];
static int var_count;
static int stack_offset;

#define MAX_GLOBALS 64
static char *global_vars[MAX_GLOBALS];
static int global_count;

#define MAX_STRINGS 64
static char *string_lits[MAX_STRINGS];
static int string_count;

#define MAX_FIELDS 16
#define MAX_STRUCTS 16
static struct {
	char name[64];
	int offset;
} struct_flds[MAX_STRUCTS][MAX_FIELDS];
static struct {
	char name[64];
	int field_count;
	int total_size;
} struct_types[MAX_STRUCTS];
static int struct_type_count;

static int lookup_struct(const char *name)
{
	int i;

	for (i = 0; i < struct_type_count; i++)
		if (strcmp(struct_types[i].name, name) == 0)
			return i;
	fprintf(stderr, "codegen: unknown struct type '%s'\n", name);
	exit(1);
	return -1;
}

static int is_global(const char *name)
{
	int i;

	for (i = 0; i < global_count; i++)
		if (strcmp(global_vars[i], name) == 0)
			return 1;
	return 0;
}

/* single scan returning pointer to the whole entry so callers read all fields at once */
static struct var_entry *lookup_var(const char *name)
{
	int i;

	for (i = var_count - 1; i >= 0; i--)
		if (strcmp(var_map[i].name, name) == 0)
			return &var_map[i];
	fprintf(stderr, "codegen: undefined variable '%s'\n", name);
	exit(1);
	return NULL;
}

/* add a new variable to the stack map -- all scalar/ptr vars use 8-byte slots */
static int declare_var(const char *name, int is_ptr, int elem_size)
{
	stack_offset -= 8;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].is_ptr = is_ptr;
	var_map[var_count].is_array = 0;
	var_map[var_count].is_struct = 0;
	var_map[var_count].elem_size = elem_size;
	var_map[var_count].struct_type[0] = '\0';
	var_count++;
	return stack_offset;
}

static int declare_struct_var(const char *name, const char *type_name)
{
	int idx;
	int bytes;

	idx = lookup_struct(type_name);
	bytes = struct_types[idx].total_size;
	if (bytes % 8 != 0)
		bytes += 8 - (bytes % 8);
	stack_offset -= bytes;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].is_ptr = 0;
	var_map[var_count].is_array = 0;
	var_map[var_count].is_struct = 1;
	var_map[var_count].elem_size = 4;
	strncpy(var_map[var_count].struct_type, type_name, 63);
	var_map[var_count].struct_type[63] = '\0';
	var_count++;
	return stack_offset;
}

/* allocate N*elem_size bytes on the stack padded to 8-byte boundary */
static int declare_array(const char *name, int size, int elem_size)
{
	int bytes = size * elem_size;

	if (bytes % 8 != 0)
		bytes += 8 - (bytes % 8);
	stack_offset -= bytes;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].is_ptr = 1;
	var_map[var_count].is_array = 1;
	var_map[var_count].is_struct = 0;
	var_map[var_count].elem_size = elem_size;
	var_map[var_count].struct_type[0] = '\0';
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

static void collect_strings(struct ast_node *node)
{
	int i;

	if (node->type == NODE_STRING) {
		for (i = 0; i < string_count; i++)
			if (strcmp(string_lits[i], node->value) == 0)
				return;
		string_lits[string_count++] = node->value;
		return;
	}
	for (i = 0; i < node->child_count; i++)
		collect_strings(node->children[i]);
}

static void gen_expression(struct ast_node *node);
static void gen_statement(struct ast_node *node);

static void gen_struct_def(struct ast_node *node)
{
	int i;
	int idx;
	int off;

	idx = struct_type_count++;
	strncpy(struct_types[idx].name, node->value, 63);
	struct_types[idx].name[63] = '\0';
	struct_types[idx].field_count = node->child_count;
	off = 0;
	for (i = 0; i < node->child_count; i++) {
		strncpy(struct_flds[idx][i].name, node->children[i]->value, 63);
		struct_flds[idx][i].name[63] = '\0';
		struct_flds[idx][i].offset = off;
		off += 4;
	}
	struct_types[idx].total_size = off;
}

static void gen_struct_decl(struct ast_node *node)
{
	declare_struct_var(node->children[0]->value, node->value);
}

static void gen_member(struct ast_node *node)
{
	struct var_entry *v;
	int idx;
	int i;
	int combined;

	v = lookup_var(node->children[0]->value);
	idx = lookup_struct(v->struct_type);
	for (i = 0; i < struct_types[idx].field_count; i++)
		if (strcmp(struct_flds[idx][i].name, node->value) == 0)
			break;
	combined = v->offset + struct_flds[idx][i].offset;
	emit("\tmovl %d(%%rbp), %%eax", combined);
}

static void gen_member_assign(struct ast_node *node)
{
	struct var_entry *v;
	int idx;
	int i;
	int combined;

	v = lookup_var(node->children[0]->value);
	idx = lookup_struct(v->struct_type);
	for (i = 0; i < struct_types[idx].field_count; i++)
		if (strcmp(struct_flds[idx][i].name, node->value) == 0)
			break;
	combined = v->offset + struct_flds[idx][i].offset;
	gen_expression(node->children[1]);
	emit("\tmovl %%eax, %d(%%rbp)", combined);
}

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

/* returns the pointer scale for this expression (0 if not a pointer)
 * int* returns 4 char* returns 1 non-pointer returns 0 */
static int expr_ptr_scale(struct ast_node *node)
{
	struct var_entry *v;
	int l;
	int r;

	switch (node->type) {
	case NODE_VAR:
		if (is_global(node->value))
			return 0;
		v = lookup_var(node->value);
		if (!v->is_ptr)
			return 0;
		return v->elem_size;
	case NODE_ADDR_OF:
		if (is_global(node->value))
			return 4;
		return lookup_var(node->value)->elem_size;
	case NODE_BINARY:
		if (node->value[1] != '\0')
			return 0;
		if (node->value[0] != '+' && node->value[0] != '-')
			return 0;
		l = expr_ptr_scale(node->children[0]);
		r = expr_ptr_scale(node->children[1]);
		/* ptr - ptr is an element count not a pointer */
		if (node->value[0] == '-' && l && r)
			return 0;
		return l ? l : r;
	default:
		return 0;
	}
}

/* pointer arithmetic -- rcx is left rax is right
 * scale the integer side by the element size before combining */
static void gen_ptr_arith(const char *op, int lscale, int rscale)
{
	if (op[0] == '+') {
		if (lscale && !rscale) {
			emit("\tmovslq %%eax, %%rax"); /* sign extend the int index */
			emit("\timulq $%d, %%rax", lscale);
		} else if (!lscale && rscale) {
			emit("\tmovslq %%ecx, %%rcx");
			emit("\timulq $%d, %%rcx", rscale);
		}
		emit("\taddq %%rcx, %%rax");
	} else if (lscale && rscale) {
		/* ptr - ptr gives the number of elements between them */
		emit("\tsubq %%rax, %%rcx");
		emit("\tmovq %%rcx, %%rax");
		emit("\tcqto");
		emit("\tmovq $%d, %%rbx", lscale);
		emit("\tidivq %%rbx");
	} else {
		/* ptr - int */
		emit("\tmovslq %%eax, %%rax");
		emit("\timulq $%d, %%rax", lscale);
		emit("\tsubq %%rax, %%rcx");
		emit("\tmovq %%rcx, %%rax");
	}
}

static void gen_binary(struct ast_node *node)
{
	const char *op = node->value;
	int lscale = expr_ptr_scale(node->children[0]);
	int rscale = expr_ptr_scale(node->children[1]);

	gen_expression(node->children[0]);
	emit("\tpush %%rax");
	gen_expression(node->children[1]);
	emit("\tpop %%rcx");

	if (is_arith_op(op) && (lscale || rscale))
		gen_ptr_arith(op, lscale, rscale);
	else if (is_arith_op(op))
		gen_arith(op);
	else if (is_compare_op(op))
		gen_compare(op);
	else
		gen_logical(op); /* && or || */
}

static void gen_var(struct ast_node *node)
{
	struct var_entry *v;

	if (is_global(node->value)) {
		emit("\tmovl %s(%%rip), %%eax", node->value);
		return;
	}
	v = lookup_var(node->value);
	if (v->is_array)
		emit("\tleaq %d(%%rbp), %%rax", v->offset);
	else if (v->is_ptr)
		emit("\tmovq %d(%%rbp), %%rax", v->offset);
	else if (v->elem_size == 1)
		emit("\tmovsbl %d(%%rbp), %%eax", v->offset);
	else
		emit("\tmovl %d(%%rbp), %%eax", v->offset);
}

static void gen_assign(struct ast_node *node)
{
	struct var_entry *v;

	gen_expression(node->children[0]);
	if (is_global(node->value)) {
		emit("\tmovl %%eax, %s(%%rip)", node->value);
		return;
	}
	v = lookup_var(node->value);
	if (v->is_ptr)
		emit("\tmovq %%rax, %d(%%rbp)", v->offset);
	else if (v->elem_size == 1)
		emit("\tmovb %%al, %d(%%rbp)", v->offset);
	else
		emit("\tmovl %%eax, %d(%%rbp)", v->offset);
}

static void gen_addr_of(struct ast_node *node)
{
	if (is_global(node->value)) {
		emit("\tleaq %s(%%rip), %%rax", node->value);
		return;
	}
	emit("\tleaq %d(%%rbp), %%rax", lookup_var(node->value)->offset);
}

/* element size the pointer points to -- used to pick load instruction */
static int expr_deref_size(struct ast_node *node)
{
	struct var_entry *v;
	int scale;

	switch (node->type) {
	case NODE_VAR:
		if (is_global(node->value))
			return 4;
		v = lookup_var(node->value);
		if (v->is_ptr)
			return v->elem_size;
		return 4;
	default:
		scale = expr_ptr_scale(node);
		return scale ? scale : 4;
	}
}

static void gen_deref(struct ast_node *node)
{
	int esz = expr_deref_size(node->children[0]);

	gen_expression(node->children[0]); /* ptr in rax */
	if (esz == 1)
		emit("\tmovsbl (%%rax), %%eax");
	else
		emit("\tmovl (%%rax), %%eax");
}

static void gen_deref_assign(struct ast_node *node)
{
	int esz = expr_deref_size(node->children[0]);

	/* child[0] = ptr expr, child[1] = value expr */
	gen_expression(node->children[1]); /* value in eax */
	emit("\tpush %%rax");
	gen_expression(node->children[0]); /* ptr in rax */
	emit("\tpop %%rcx");
	if (esz == 1)
		emit("\tmovb %%cl, (%%rax)");
	else
		emit("\tmovl %%ecx, (%%rax)");
}

static void gen_ptr_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 1, 4);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovq %%rax, %d(%%rbp)", offset);
	}
}

static void gen_char_ptr_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 1, 1);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovq %%rax, %d(%%rbp)", offset);
	}
}

/* delta is +1 or -1
 * post: load old value before mutating (postfix semantics) */
static void gen_inc_dec(struct ast_node *node, int delta, int post)
{
	struct var_entry *v;
	const char *op;
	int off;
	int esz;

	op = (delta > 0) ? "add" : "sub";
	if (is_global(node->value)) {
		if (post)
			emit("\tmovl %s(%%rip), %%eax", node->value);
		emit("\t%sl $1, %s(%%rip)", op, node->value);
		if (!post)
			emit("\tmovl %s(%%rip), %%eax", node->value);
		return;
	}
	v = lookup_var(node->value);
	off = v->offset;
	esz = v->elem_size;
	if (post) {
		if (v->is_ptr)     emit("\tmovq %d(%%rbp), %%rax", off);
		else if (esz == 1) emit("\tmovsbl %d(%%rbp), %%eax", off);
		else               emit("\tmovl %d(%%rbp), %%eax", off);
	}
	if (v->is_ptr)         emit("\t%sq $%d, %d(%%rbp)", op, esz, off);
	else if (esz == 1)     emit("\t%sb $1, %d(%%rbp)", op, off);
	else                   emit("\t%sl $1, %d(%%rbp)", op, off);
	if (!post) {
		if (v->is_ptr)     emit("\tmovq %d(%%rbp), %%rax", off);
		else if (esz == 1) emit("\tmovsbl %d(%%rbp), %%eax", off);
		else               emit("\tmovl %d(%%rbp), %%eax", off);
	}
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

/* cond ? a : b like if/else but the chosen branch lands in eax */
static void gen_string(struct ast_node *node)
{
	int i;

	for (i = 0; i < string_count; i++)
		if (strcmp(string_lits[i], node->value) == 0)
			break;
	emit("\tleaq .LC%d(%%rip), %%rax", i);
}

static void gen_ternary(struct ast_node *node)
{
	int lbl = label_count++;

	gen_expression(node->children[0]);
	emit("\tcmpl $0, %%eax");
	emit("\tje .Lternfalse%d", lbl);
	gen_expression(node->children[1]); /* true branch */
	emit("\tjmp .Lternend%d", lbl);
	emit(".Lternfalse%d:", lbl);
	gen_expression(node->children[2]); /* false branch */
	emit(".Lternend%d:", lbl);
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
	case NODE_PREFIX_INC:		gen_inc_dec(node, +1, 0);	break;
	case NODE_PREFIX_DEC:		gen_inc_dec(node, -1, 0);	break;
	case NODE_POSTFIX_INC:		gen_inc_dec(node, +1, 1);	break;
	case NODE_POSTFIX_DEC:		gen_inc_dec(node, -1, 1);	break;
	case NODE_ADDR_OF:		gen_addr_of(node);		break;
	case NODE_DEREF:		gen_deref(node);		break;
	case NODE_DEREF_ASSIGN:		gen_deref_assign(node);		break;
	case NODE_TERNARY:		gen_ternary(node);		break;
	case NODE_STRING:		gen_string(node);		break;
	case NODE_MEMBER:		gen_member(node);		break;
	case NODE_MEMBER_ASSIGN:	gen_member_assign(node);	break;
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
	int offset = declare_var(node->value, 0, 4);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovl %%eax, %d(%%rbp)", offset);
	}
}

static void gen_char_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 0, 1);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovb %%al, %d(%%rbp)", offset);
	}
}

static void gen_array_decl(struct ast_node *node)
{
	declare_array(node->value, atoi(node->children[0]->value), 4);
}

static void gen_char_array_decl(struct ast_node *node)
{
	declare_array(node->value, atoi(node->children[0]->value), 1);
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

	gen_statement(node->children[0]);		/* init: expr or declaration */
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
	case NODE_RETURN:		gen_return(node);		break;
	case NODE_DECLARATION:		gen_declaration(node);		break;
	case NODE_CHAR_DECLARATION:	gen_char_declaration(node);	break;
	case NODE_PTR_DECLARATION:	gen_ptr_declaration(node);	break;
	case NODE_CHAR_PTR_DECLARATION:	gen_char_ptr_declaration(node);	break;
	case NODE_ARRAY_DECL:		gen_array_decl(node);		break;
	case NODE_CHAR_ARRAY_DECL:	gen_char_array_decl(node);	break;
	case NODE_STRUCT_DECL:		gen_struct_decl(node);		break;
	case NODE_COMPOUND:		gen_compound(node);		break;
	case NODE_IF:			gen_if(node);			break;
	case NODE_WHILE:		gen_while(node);		break;
	case NODE_FOR:			gen_for(node);			break;
	case NODE_BREAK:	emit("\tjmp %s", loop_break_label); break;
	case NODE_CONTINUE:	emit("\tjmp %s", loop_cont_label);  break;
	case NODE_ASSIGN:
	case NODE_DEREF_ASSIGN:
	case NODE_VAR:
	case NODE_BINARY:
	case NODE_UNARY:
	case NODE_NUMBER:
	case NODE_CALL:
	case NODE_PREFIX_INC:
	case NODE_PREFIX_DEC:
	case NODE_POSTFIX_INC:
	case NODE_POSTFIX_DEC:
	case NODE_DEREF:
	case NODE_TERNARY:
	case NODE_STRING:
	case NODE_MEMBER:
	case NODE_MEMBER_ASSIGN:
		gen_expression(node);
		break;
	default:
		fprintf(stderr, "codegen: bad statement node type %d\n",
				node->type);
		exit(1);
	}
}

/* count bytes needed for locals so the right amount of stack gets reserved */
static int count_stack_bytes(struct ast_node *node)
{
	int i;
	int total = 0;
	int n;
	int bytes;
	int sidx;

	if (node->type == NODE_DECLARATION
			|| node->type == NODE_PTR_DECLARATION
			|| node->type == NODE_CHAR_DECLARATION
			|| node->type == NODE_CHAR_PTR_DECLARATION)
		return 8;
	if (node->type == NODE_ARRAY_DECL) {
		n = atoi(node->children[0]->value);
		bytes = n * 4;
		if (bytes % 8 != 0)
			bytes += 8 - (bytes % 8);
		return bytes;
	}
	if (node->type == NODE_CHAR_ARRAY_DECL) {
		n = atoi(node->children[0]->value);
		bytes = n * 1;
		if (bytes % 8 != 0)
			bytes += 8 - (bytes % 8);
		return bytes;
	}
	if (node->type == NODE_STRUCT_DECL) {
		sidx = lookup_struct(node->value);
		bytes = struct_types[sidx].total_size;
		if (bytes % 8 != 0)
			bytes += 8 - (bytes % 8);
		return bytes;
	}
	for (i = 0; i < node->child_count; i++)
		total += count_stack_bytes(node->children[i]);
	return total;
}

static void gen_function(struct ast_node *node)
{
	static const char *param_regs[] = {
		"%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"
	};
	static const char *ptr_param_regs[] = {
		"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
	};
	static const char *byte_param_regs[] = {
		"%dil", "%sil", "%dl", "%cl", "%r8b", "%r9b"
	};
	int i;
	int num_params;
	int num_locals;
	int alloc_size;
	int offset;
	int is_ptr_param;
	int is_char_param;
	int elem_sz;
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

	/* reserve stack for params and locals aligned to 16 */
	num_locals = count_stack_bytes(body);
	alloc_size = num_params * 8 + num_locals;
	if (alloc_size > 0) {
		if (alloc_size % 16 != 0)
			alloc_size += 16 - (alloc_size % 16);
		emit("\tsubq $%d, %%rsp", alloc_size);
	}

	/* copy params from argument registers onto the stack */
	for (i = 0; i < num_params && i < 6; i++) {
		is_ptr_param = node->children[i]->type == NODE_PTR_DECLARATION
				|| node->children[i]->type == NODE_CHAR_PTR_DECLARATION;
		is_char_param = node->children[i]->type == NODE_CHAR_DECLARATION
				|| node->children[i]->type == NODE_CHAR_PTR_DECLARATION;
		elem_sz = is_char_param ? 1 : 4;
		offset = declare_var(node->children[i]->value, is_ptr_param, elem_sz);
		if (is_ptr_param)
			emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[i], offset);
		else if (is_char_param)
			emit("\tmovb %s, %d(%%rbp)", byte_param_regs[i], offset);
		else
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

static void gen_global(struct ast_node *node)
{
	int val = node->child_count > 0 ? atoi(node->children[0]->value) : 0;

	emit("\t.globl %s", node->value);
	emit("\t.align 4");
	emit("%s:", node->value);
	emit("\t.long %d", val);
}

static void gen_program(struct ast_node *node)
{
	int i;
	int has_globals = 0;

	collect_strings(node);

	for (i = 0; i < node->child_count; i++)
		if (node->children[i]->type == NODE_STRUCT_DEF)
			gen_struct_def(node->children[i]);

	for (i = 0; i < node->child_count; i++)
		if (node->children[i]->type == NODE_GLOBAL)
			global_vars[global_count++] =
				strdup(node->children[i]->value);

	if (string_count > 0) {
		emit("\t.section .rodata");
		for (i = 0; i < string_count; i++) {
			emit(".LC%d:", i);
			emit("\t.string \"%s\"", string_lits[i]);
		}
	}

	for (i = 0; i < node->child_count; i++) {
		if (node->children[i]->type == NODE_GLOBAL) {
			if (!has_globals) {
				emit("\t.data");
				has_globals = 1;
			}
			gen_global(node->children[i]);
		}
	}

	emit("\t.text");
	for (i = 0; i < node->child_count; i++)
		if (node->children[i]->type == NODE_FUNCTION)
			gen_function(node->children[i]);
	emit("\t.section .note.GNU-stack,\"\",@progbits");
}

void codegen(struct ast_node *ast, FILE *output)
{
	out = output;
	gen_program(ast);
}
