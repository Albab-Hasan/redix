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

enum type_kind {
	TY_INT,
	TY_CHAR,
	TY_LONG,
	TY_VOID,
	TY_PTR,
	TY_ARRAY,
	TY_STRUCT,
	TY_FUNC
};

/* size stays 0 for structs and arrays since those resolve from the tag or the length */
struct type {
	enum type_kind kind;
	int size;
	int is_unsigned;
	struct type *base;   /* pointee of a pointer or element of an array */
	int len;
	char tag[64];
};

#define MAX_VARS 128
static struct var_entry {
	char *name;
	int offset;      /* negative offset from rbp */
	struct type *ty;
} var_map[MAX_VARS];
static int var_count;
static int stack_offset;

#define MAX_FUNCS 64
static char *func_names[MAX_FUNCS];
static int func_count;

/* globals get the same treatment as var_map so pointer and array
 * globals remember what they point at */
#define MAX_GLOBALS 64
static struct glob_entry {
	char *name;
	struct type *ty;
} global_map[MAX_GLOBALS];
static int global_count;

#define MAX_STRINGS 64
static char *string_lits[MAX_STRINGS];
static int string_count;

#define MAX_FIELDS 16
#define MAX_STRUCTS 16
static struct field_entry {
	char name[64];
	int offset;
	struct type *ty;
} struct_flds[MAX_STRUCTS][MAX_FIELDS];
static struct {
	char name[64];
	int field_count;
	int total_size;
} struct_types[MAX_STRUCTS];
static int struct_type_count;

/* maps function names to their struct return size so call sites know rax:rdx is live */
#define MAX_SRET 32
static struct {
	char name[64];
	int  total_size;
} sret_tab[MAX_SRET];
static int sret_count;
static char current_func_sret_type[64];

/* a pointer returning function tells call sites nothing by itself so the type is kept here */
#define MAX_RETPTR 32
static struct {
	char name[64];
	struct type *ty;
} retptr_tab[MAX_RETPTR];
static int retptr_count;

/* call sites must zero al before a variadic callee so the registry has to reach them */
#define MAX_VARARG 32
static char *vararg_names[MAX_VARARG];
static int vararg_count;

/* 6 saved integer regs and the 4 fields the abi lays out for va_list */
#define VA_SAVE_SIZE 48
#define VA_LIST_SIZE 24
static int current_va_save;     /* rbp offset of the register save area */
static int current_va_gp_start; /* bytes of the save area the named params already ate */

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

/* the scalars are shared since nothing ever mutates a type once it is built */
static struct type *ty_int;
static struct type *ty_char;
static struct type *ty_long;
static struct type *ty_uint;
static struct type *ty_uchar;
static struct type *ty_ulong;
static struct type *ty_void;
static struct type *ty_func;

static struct type *new_type(enum type_kind kind, int size)
{
	struct type *ty = malloc(sizeof(struct type));

	ty->kind = kind;
	ty->size = size;
	ty->is_unsigned = 0;
	ty->base = NULL;
	ty->len = 0;
	ty->tag[0] = '\0';
	return ty;
}

static void init_types(void)
{
	ty_int = new_type(TY_INT, 4);
	ty_char = new_type(TY_CHAR, 1);
	ty_long = new_type(TY_LONG, 8);
	ty_uint = new_type(TY_INT, 4);
	ty_uint->is_unsigned = 1;
	ty_uchar = new_type(TY_CHAR, 1);
	ty_uchar->is_unsigned = 1;
	ty_ulong = new_type(TY_LONG, 8);
	ty_ulong->is_unsigned = 1;
	/* void has no size so a void pointer steps a byte at a time like gcc does */
	ty_void = new_type(TY_VOID, 1);
	ty_func = new_type(TY_FUNC, 8);
}

static struct type *ptr_to(struct type *base)
{
	struct type *ty = new_type(TY_PTR, 8);

	ty->base = base;
	return ty;
}

static struct type *array_of(struct type *base, int len)
{
	struct type *ty = new_type(TY_ARRAY, 0);

	ty->base = base;
	ty->len = len;
	return ty;
}

/* the tag is kept unresolved so a struct can hold a pointer to itself */
static struct type *struct_of(const char *tag)
{
	struct type *ty = new_type(TY_STRUCT, 0);

	strncpy(ty->tag, tag, 63);
	ty->tag[63] = '\0';
	return ty;
}

static int type_size(struct type *ty)
{
	if (ty->kind == TY_STRUCT)
		return struct_types[lookup_struct(ty->tag)].total_size;
	if (ty->kind == TY_ARRAY)
		return ty->len * type_size(ty->base);
	return ty->size;
}

/* an array decays to the address of its first element so both index the same way */
static int is_ptr_like(struct type *ty)
{
	return ty && (ty->kind == TY_PTR || ty->kind == TY_ARRAY);
}

/* the byte step of one index 0 when the operand is not an address */
static int ptr_scale(struct type *ty)
{
	if (!is_ptr_like(ty))
		return 0;
	/* a function pointer has no elements to walk through */
	if (ty->base->kind == TY_FUNC)
		return 0;
	return type_size(ty->base);
}

/* a flat brace list fills an array at the innermost element size */
static struct type *elem_scalar(struct type *ty)
{
	while (ty->kind == TY_ARRAY)
		ty = ty->base;
	return ty;
}

static struct field_entry *lookup_field(int idx, const char *name)
{
	int i;

	for (i = 0; i < struct_types[idx].field_count; i++)
		if (strcmp(struct_flds[idx][i].name, name) == 0)
			return &struct_flds[idx][i];
	fprintf(stderr, "codegen: struct '%s' has no field '%s'\n",
			struct_types[idx].name, name);
	exit(1);
	return NULL;
}

static int lookup_retptr(const char *name)
{
	int i;

	for (i = 0; i < retptr_count; i++)
		if (strcmp(retptr_tab[i].name, name) == 0)
			return i;
	return -1;
}

/* the spelling names the base type and depth counts the stars the parser saw */
static struct type *type_of_spelling(const char *spelling, int depth)
{
	struct type *ty;

	if (strcmp(spelling, "char") == 0)               ty = ty_char;
	else if (strcmp(spelling, "int") == 0)           ty = ty_int;
	else if (strcmp(spelling, "long") == 0)          ty = ty_long;
	else if (strcmp(spelling, "unsigned") == 0)      ty = ty_uint;
	else if (strcmp(spelling, "unsigned_char") == 0) ty = ty_uchar;
	else if (strcmp(spelling, "void") == 0)          ty = ty_void;
	/* anything left is a struct tag since every base type name is a keyword */
	else                                             ty = struct_of(spelling);
	while (depth-- > 0)
		ty = ptr_to(ty);
	return ty;
}

static void declare_retptr(const char *name, const char *pointee, int depth)
{
	strncpy(retptr_tab[retptr_count].name, name, 63);
	retptr_tab[retptr_count].name[63] = '\0';
	retptr_tab[retptr_count].ty =
			type_of_spelling(pointee, depth < 1 ? 1 : depth);
	retptr_count++;
}

/* like lookup_var but returns NULL instead of dying since most names are locals */
static struct glob_entry *lookup_global(const char *name)
{
	int i;

	for (i = 0; i < global_count; i++)
		if (strcmp(global_map[i].name, name) == 0)
			return &global_map[i];
	return NULL;
}

static int is_global(const char *name)
{
	return lookup_global(name) != NULL;
}

/* the dims past the first hang off the size node so the shape reads left to right from there */
static struct type *array_type(struct ast_node *size, struct type *base)
{
	int i;

	for (i = size->child_count - 1; i >= 0; i--)
		base = array_of(base, atoi(size->children[i]->value));
	return array_of(base, atoi(size->value));
}

/* a pointer node the parser never gave a depth to is a plain single star */
static struct type *decl_ptr_type(struct ast_node *node, struct type *base)
{
	int depth = node->ptr_depth < 1 ? 1 : node->ptr_depth;

	while (depth-- > 0)
		base = ptr_to(base);
	return base;
}

/* the frame counter and the code that stores into the slot both ask
 * here so they cannot disagree about how wide a local is */
static struct type *decl_type(struct ast_node *node)
{
	switch (node->type) {
	case NODE_DECLARATION:
	case NODE_GLOBAL:			return ty_int;
	case NODE_CHAR_DECLARATION:		return ty_char;
	case NODE_LONG_DECLARATION:		return ty_long;
	case NODE_UNSIGNED_DECLARATION:		return ty_uint;
	case NODE_UNSIGNED_CHAR_DECLARATION:	return ty_uchar;
	case NODE_PTR_DECLARATION:
	case NODE_GLOBAL_PTR:			return decl_ptr_type(node, ty_int);
	case NODE_CHAR_PTR_DECLARATION:
	case NODE_GLOBAL_CHAR_PTR:		return decl_ptr_type(node, ty_char);
	case NODE_FPTR_DECLARATION:		return ptr_to(ty_func);
	/* the four abi fields fit three quadwords and the array makes the name decay */
	case NODE_VA_LIST_DECL:			return array_of(ty_long, 3);
	case NODE_ARRAY_DECL:
	case NODE_GLOBAL_ARRAY:
		return array_type(node->children[0], ty_int);
	case NODE_CHAR_ARRAY_DECL:
	case NODE_GLOBAL_CHAR_ARRAY:
		return array_type(node->children[0], ty_char);
	case NODE_PTR_ARRAY_DECL:
	case NODE_GLOBAL_PTR_ARRAY:
		return array_type(node->children[0], decl_ptr_type(node, ty_int));
	case NODE_CHAR_PTR_ARRAY_DECL:
	case NODE_GLOBAL_CHAR_PTR_ARRAY:
		return array_type(node->children[0], decl_ptr_type(node, ty_char));
	case NODE_STRUCT_DECL:
	case NODE_STRUCT_VAL_PARAM:
	case NODE_GLOBAL_STRUCT:		return struct_of(node->value);
	case NODE_STRUCT_PTR_DECL:
	case NODE_GLOBAL_STRUCT_PTR:
		return decl_ptr_type(node, struct_of(node->value));
	case NODE_STRUCT_ARRAY_DECL:
	case NODE_GLOBAL_STRUCT_ARRAY:
		return array_of(struct_of(node->value),
				atoi(node->children[1]->value));
	default:				return NULL;
	}
}

/* struct declarations keep the type in value so the name lives in the first child */
static const char *decl_name(struct ast_node *node)
{
	switch (node->type) {
	case NODE_STRUCT_DECL:
	case NODE_STRUCT_PTR_DECL:
	case NODE_STRUCT_ARRAY_DECL:
	case NODE_STRUCT_VAL_PARAM:
	case NODE_GLOBAL_STRUCT:
	case NODE_GLOBAL_STRUCT_PTR:
	case NODE_GLOBAL_STRUCT_ARRAY:
		return node->children[0]->value;
	default:
		return node->value;
	}
}

static void declare_global(const char *name, struct type *ty)
{
	global_map[global_count].name = strdup(name);
	global_map[global_count].ty = ty;
	global_count++;
}

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

/* like lookup_var but returns NULL on miss so callers can test without dying */
static struct var_entry *try_lookup_var(const char *name)
{
	int i;

	for (i = var_count - 1; i >= 0; i--)
		if (strcmp(var_map[i].name, name) == 0)
			return &var_map[i];
	return NULL;
}

/* the smallest slot is 8 and wider ones round up so later offsets stay aligned */
static int slot_bytes(struct type *ty)
{
	int bytes = type_size(ty);

	if (bytes < 8)
		bytes = 8;
	if (bytes % 8 != 0)
		bytes += 8 - (bytes % 8);
	return bytes;
}

static int declare_var(const char *name, struct type *ty)
{
	stack_offset -= slot_bytes(ty);
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].ty = ty;
	var_count++;
	return stack_offset;
}

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

static int string_label(const char *s)
{
	int i;

	for (i = 0; i < string_count; i++)
		if (strcmp(string_lits[i], s) == 0)
			return i;
	return 0;
}

/* a global has to be a literal by assembly time so its initializer folds here */
static int const_eval(struct ast_node *node)
{
	const char *op;
	int l;
	int r;

	if (node->type == NODE_NUMBER)
		return atoi(node->value);
	if (node->type == NODE_UNARY) {
		l = const_eval(node->children[0]);
		if (node->value[0] == '-')
			return -l;
		if (node->value[0] == '~')
			return ~l;
		return !l;
	}
	if (node->type != NODE_BINARY) {
		fprintf(stderr, "codegen: global initializer is not a constant\n");
		exit(1);
	}
	op = node->value;
	l = const_eval(node->children[0]);
	r = const_eval(node->children[1]);
	if (strcmp(op, "+") == 0)	return l + r;
	if (strcmp(op, "-") == 0)	return l - r;
	if (strcmp(op, "*") == 0)	return l * r;
	if (strcmp(op, "/") == 0)	return l / r;
	if (strcmp(op, "%") == 0)	return l % r;
	if (strcmp(op, "<<") == 0)	return l << r;
	if (strcmp(op, ">>") == 0)	return l >> r;
	if (strcmp(op, "&") == 0)	return l & r;
	if (strcmp(op, "|") == 0)	return l | r;
	if (strcmp(op, "^") == 0)	return l ^ r;
	fprintf(stderr, "codegen: operator '%s' not allowed in a global initializer\n",
			op);
	exit(1);
	return 0;
}

static void gen_expression(struct ast_node *node);
static void gen_statement(struct ast_node *node);

static void gen_struct_def(struct ast_node *node)
{
	struct ast_node *f;
	struct type *fty;
	int i;
	int idx;
	int off;
	int fsz;
	int max_align;

	idx = struct_type_count++;
	strncpy(struct_types[idx].name, node->value, 63);
	struct_types[idx].name[63] = '\0';
	struct_types[idx].field_count = node->child_count;
	off = 0;
	max_align = 1;
	for (i = 0; i < node->child_count; i++) {
		f = node->children[i];
		fty = decl_type(f);
		fsz = type_size(fty);
		/* char fields pack tight wider fields align to their size so loads never straddle */
		if (off % fsz != 0)
			off += fsz - (off % fsz);
		strncpy(struct_flds[idx][i].name, decl_name(f), 63);
		struct_flds[idx][i].name[63] = '\0';
		struct_flds[idx][i].offset = off;
		struct_flds[idx][i].ty = fty;
		off += fsz;
		if (fsz > max_align)
			max_align = fsz;
	}
	/* total rounds up to the widest field so back to back structs would keep their fields aligned */
	if (off % max_align != 0)
		off += max_align - (off % max_align);
	struct_types[idx].total_size = off;
}

static struct type *expr_type(struct ast_node *node);
static int is_func(const char *name);

/* the struct tag an expression denotes or NULL when there is not one */
static const char *expr_struct_type(struct ast_node *node)
{
	struct type *ty = expr_type(node);

	while (is_ptr_like(ty))
		ty = ty->base;
	if (ty && ty->kind == TY_STRUCT)
		return ty->tag;
	return NULL;
}

static struct field_entry *expr_field(struct ast_node *node)
{
	const char *t = expr_struct_type(node->children[0]);

	if (!t)
		return NULL;
	return lookup_field(lookup_struct(t), node->value);
}

/* the object is either a named variable or an a[i] element */
static void gen_obj_addr(struct ast_node *node)
{
	if (node->type == NODE_DEREF) {
		gen_expression(node->children[0]);
		return;
	}
	if (node->type != NODE_VAR) {
		fprintf(stderr, "codegen: not an lvalue\n");
		exit(1);
	}
	if (is_global(node->value)) {
		emit("\tleaq %s(%%rip), %%rax", node->value);
		return;
	}
	emit("\tleaq %d(%%rbp), %%rax", lookup_var(node->value)->offset);
}

/* leaves the field address in rax and hands back the field so callers pick the width */
static struct field_entry *gen_member_addr(struct ast_node *node)
{
	struct field_entry *f;

	f = expr_field(node);
	if (!f) {
		fprintf(stderr, "codegen: no struct type for member '%s'\n",
				node->value);
		exit(1);
	}
	/* arrow starts from a pointer value dot starts from the object itself */
	if (node->type == NODE_PTR_MEMBER || node->type == NODE_PTR_MEMBER_ASSIGN)
		gen_expression(node->children[0]);
	else
		gen_obj_addr(node->children[0]);
	if (f->offset != 0)
		emit("\taddq $%d, %%rax", f->offset);
	return f;
}

static void gen_member_load(struct ast_node *node)
{
	int sz = type_size(gen_member_addr(node)->ty);

	if (sz == 8)      emit("\tmovq (%%rax), %%rax");
	else if (sz == 1) emit("\tmovsbl (%%rax), %%eax");
	else              emit("\tmovl (%%rax), %%eax");
}

static void gen_member_store(struct ast_node *node)
{
	int sz;

	gen_expression(node->children[1]);
	emit("\tpush %%rax");
	sz = type_size(gen_member_addr(node)->ty);
	emit("\tpop %%rcx");
	if (sz == 8)      emit("\tmovq %%rcx, (%%rax)");
	else if (sz == 1) emit("\tmovb %%cl, (%%rax)");
	else              emit("\tmovl %%ecx, (%%rax)");
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

/* ecx holds left eax holds right */
static void gen_arith(const char *op, int is_uns, int is_long)
{
	if (strcmp(op, "+") == 0) {
		emit(is_long ? "\taddq %%rcx, %%rax" : "\taddl %%ecx, %%eax");
	} else if (strcmp(op, "-") == 0) {
		if (is_long) {
			emit("\tsubq %%rax, %%rcx");
			emit("\tmovq %%rcx, %%rax");
		} else {
			emit("\tsubl %%eax, %%ecx");
			emit("\tmovl %%ecx, %%eax");
		}
	} else if (strcmp(op, "*") == 0) {
		emit(is_long ? "\timulq %%rcx, %%rax" : "\timull %%ecx, %%eax");
	} else if (strcmp(op, "/") == 0) {
		if (is_long && is_uns) {
			emit("\tmovq %%rax, %%rbx");
			emit("\tmovq %%rcx, %%rax");
			emit("\txorq %%rdx, %%rdx");
			emit("\tdivq %%rbx");
		} else if (is_long) {
			emit("\tmovq %%rax, %%rbx");
			emit("\tmovq %%rcx, %%rax");
			emit("\tcqto");
			emit("\tidivq %%rbx");
		} else if (is_uns) {
			emit("\tmovl %%eax, %%ebx");
			emit("\tmovl %%ecx, %%eax");
			emit("\txorl %%edx, %%edx");
			emit("\tdivl %%ebx");
		} else {
			emit("\tmovl %%eax, %%ebx");
			emit("\tmovl %%ecx, %%eax");
			emit("\tcdq");
			emit("\tidivl %%ebx");
		}
	} else if (strcmp(op, "%") == 0) {
		if (is_long && is_uns) {
			emit("\tmovq %%rax, %%rbx");
			emit("\tmovq %%rcx, %%rax");
			emit("\txorq %%rdx, %%rdx");
			emit("\tdivq %%rbx");
			emit("\tmovq %%rdx, %%rax");
		} else if (is_long) {
			emit("\tmovq %%rax, %%rbx");
			emit("\tmovq %%rcx, %%rax");
			emit("\tcqto");
			emit("\tidivq %%rbx");
			emit("\tmovq %%rdx, %%rax");
		} else if (is_uns) {
			emit("\tmovl %%eax, %%ebx");
			emit("\tmovl %%ecx, %%eax");
			emit("\txorl %%edx, %%edx");
			emit("\tdivl %%ebx");
			emit("\tmovl %%edx, %%eax");
		} else {
			emit("\tmovl %%eax, %%ebx");
			emit("\tmovl %%ecx, %%eax");
			emit("\tcdq");
			emit("\tidivl %%ebx");
			emit("\tmovl %%edx, %%eax");
		}
	}
}

/* ecx holds left eax holds right */
static void gen_compare(const char *op, int is_uns, int is_long)
{
	emit(is_long ? "\tcmpq %%rax, %%rcx" : "\tcmpl %%eax, %%ecx");
	emit("\tmovl $0, %%eax");
	if (strcmp(op, "<") == 0)
		emit(is_uns ? "\tsetb %%al"  : "\tsetl %%al");
	else if (strcmp(op, "<=") == 0)
		emit(is_uns ? "\tsetbe %%al" : "\tsetle %%al");
	else if (strcmp(op, ">") == 0)
		emit(is_uns ? "\tseta %%al"  : "\tsetg %%al");
	else if (strcmp(op, ">=") == 0)
		emit(is_uns ? "\tsetae %%al" : "\tsetge %%al");
	else if (strcmp(op, "==") == 0) emit("\tsete %%al");
	else if (strcmp(op, "!=") == 0) emit("\tsetne %%al");
}

/* anding raw values would get 2 && 4 wrong so both sides normalize to 0 or 1 first */
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
			|| op[0] == '*' || op[0] == '/' || op[0] == '%');
}

static int is_compare_op(const char *op)
{
	char c = op[0];
	if (c == '<' || c == '>') return op[1] == '=' || op[1] == '\0';
	return c == '=' || c == '!';
}

static int is_bitwise_op(const char *op)
{
	return (op[1] == '\0' && (op[0] == '&' || op[0] == '|' || op[0] == '^'))
		|| (op[0] == '<' && op[1] == '<')
		|| (op[0] == '>' && op[1] == '>');
}

/* ecx holds left eax holds right -- sal and sar want the count in cl */
static void gen_bitwise(const char *op, int is_uns, int is_long)
{
	if (op[0] == '&') {
		emit(is_long ? "\tandq %%rcx, %%rax" : "\tandl %%ecx, %%eax");
	} else if (op[0] == '|') {
		emit(is_long ? "\torq %%rcx, %%rax"  : "\torl %%ecx, %%eax");
	} else if (op[0] == '^') {
		emit(is_long ? "\txorq %%rcx, %%rax" : "\txorl %%ecx, %%eax");
	} else if (op[0] == '<') {
		emit("\txchg %%eax, %%ecx");
		emit(is_long ? "\tsalq %%cl, %%rax"  : "\tsall %%cl, %%eax");
	} else {
		/* unsigned right shift does not sign-fill */
		emit("\txchg %%eax, %%ecx");
		if (is_long && is_uns)  emit("\tshrq %%cl, %%rax");
		else if (is_long)       emit("\tsarq %%cl, %%rax");
		else if (is_uns)        emit("\tshrl %%cl, %%eax");
		else                    emit("\tsarl %%cl, %%eax");
	}
}

/* pointer arithmetic keeps the pointer side and the usual widening picks the rest */
static struct type *binary_type(struct ast_node *node)
{
	struct type *l = expr_type(node->children[0]);
	struct type *r = expr_type(node->children[1]);
	int uns = (l && l->is_unsigned) || (r && r->is_unsigned);

	if (node->value[1] == '\0'
			&& (node->value[0] == '+' || node->value[0] == '-')) {
		/* ptr minus ptr counts elements so the result is a plain int */
		if (is_ptr_like(l) && is_ptr_like(r))
			return node->value[0] == '-' ? ty_int : l;
		if (is_ptr_like(l))
			return l;
		if (is_ptr_like(r))
			return r;
	}
	if ((l && l->kind == TY_LONG) || (r && r->kind == TY_LONG))
		return uns ? ty_ulong : ty_long;
	return uns ? ty_uint : ty_int;
}

/* NULL when the node carries no type so every caller has to check */
static struct type *expr_type(struct ast_node *node)
{
	struct var_entry *v;
	struct glob_entry *g;
	struct field_entry *f;
	struct type *ty;
	int idx;

	switch (node->type) {
	case NODE_NUMBER:
	case NODE_SIZEOF_STRUCT:
		return ty_int;
	case NODE_STRING:
		return ptr_to(ty_char);
	/* inc dec and assign all keep the target name in value like a plain var */
	case NODE_VAR:
	case NODE_ASSIGN:
	case NODE_PREFIX_INC:
	case NODE_PREFIX_DEC:
	case NODE_POSTFIX_INC:
	case NODE_POSTFIX_DEC:
		/* anything that is not a plain name rides in a child instead */
		if (node->child_count > 0)
			return expr_type(node->children[0]);
		g = lookup_global(node->value);
		if (g)
			return g->ty;
		v = try_lookup_var(node->value);
		if (v)
			return v->ty;
		/* a name that is neither has to be a function used as a value */
		return is_func(node->value) ? ptr_to(ty_func) : NULL;
	case NODE_DEREF:
	case NODE_DEREF_ASSIGN:
		ty = expr_type(node->children[0]);
		return is_ptr_like(ty) ? ty->base : ty_int;
	case NODE_ADDR_OF:
		/* &a[i] and &s.f carry the operand as a child since there is no plain name to take */
		if (node->child_count > 0)
			return ptr_to(expr_type(node->children[0]));
		g = lookup_global(node->value);
		if (g)
			return ptr_to(g->ty);
		v = try_lookup_var(node->value);
		return ptr_to(v ? v->ty : ty_int);
	case NODE_MEMBER:
	case NODE_MEMBER_ASSIGN:
	case NODE_PTR_MEMBER:
	case NODE_PTR_MEMBER_ASSIGN:
		f = expr_field(node);
		return f ? f->ty : NULL;
	case NODE_CALL:
		idx = lookup_retptr(node->value);
		return idx < 0 ? ty_int : retptr_tab[idx].ty;
	case NODE_CAST:
	case NODE_VA_ARG:
		return type_of_spelling(node->value, node->ptr_depth);
	case NODE_UNARY:
		return expr_type(node->children[0]);
	case NODE_TERNARY:
		ty = expr_type(node->children[1]);
		return ty ? ty : expr_type(node->children[2]);
	case NODE_BINARY:
		return binary_type(node);
	default:
		return NULL;
	}
}

static int expr_ptr_scale(struct ast_node *node)
{
	return ptr_scale(expr_type(node));
}

static int expr_is_long(struct ast_node *node)
{
	struct type *ty = expr_type(node);

	return ty && ty->kind == TY_LONG;
}

static int expr_is_unsigned(struct ast_node *node)
{
	struct type *ty = expr_type(node);

	return ty && ty->is_unsigned;
}

/* rcx holds left rax holds right
 * the integer side gets scaled by the element size before combining */
static void gen_ptr_arith(const char *op, int lscale, int rscale)
{
	if (op[0] == '+') {
		if (lscale && !rscale) {
			emit("\tmovslq %%eax, %%rax"); /* index is 32 bit but address math is 64 bit */
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
	int is_uns = expr_is_unsigned(node->children[0])
			|| expr_is_unsigned(node->children[1]);
	int is_long = expr_is_long(node->children[0])
			|| expr_is_long(node->children[1]);

	gen_expression(node->children[0]);
	emit("\tpush %%rax");
	gen_expression(node->children[1]);
	emit("\tpop %%rcx");

	if (is_arith_op(op) && (lscale || rscale))
		gen_ptr_arith(op, lscale, rscale);
	else if (is_arith_op(op))
		gen_arith(op, is_uns, is_long);
	else if (is_compare_op(op))
		gen_compare(op, is_uns, is_long);
	else if (is_bitwise_op(op))
		gen_bitwise(op, is_uns, is_long);
	else
		gen_logical(op);
}

static void emit_load(int off, struct type *ty)
{
	int esz = type_size(ty);

	if (ty->kind == TY_PTR)                 emit("\tmovq %d(%%rbp), %%rax", off);
	else if (esz == 8)                      emit("\tmovq %d(%%rbp), %%rax", off);
	else if (esz == 1 && ty->is_unsigned)   emit("\tmovzbl %d(%%rbp), %%eax", off);
	else if (esz == 1)                      emit("\tmovsbl %d(%%rbp), %%eax", off);
	else                                    emit("\tmovl %d(%%rbp), %%eax", off);
}

static void emit_store(int off, struct type *ty)
{
	int esz = type_size(ty);

	if (ty->kind == TY_PTR) emit("\tmovq %%rax, %d(%%rbp)", off);
	else if (esz == 8)      emit("\tmovq %%rax, %d(%%rbp)", off);
	else if (esz == 1)      emit("\tmovb %%al, %d(%%rbp)", off);
	else                    emit("\tmovl %%eax, %d(%%rbp)", off);
}

static int is_func(const char *name)
{
	int i;

	for (i = 0; i < func_count; i++)
		if (strcmp(func_names[i], name) == 0)
			return 1;
	return 0;
}

static void gen_var(struct ast_node *node)
{
	struct var_entry *v;
	struct glob_entry *g;

	/* function name used as a value decays to its address */
	if (is_func(node->value)) {
		emit("\tleaq %s(%%rip), %%rax", node->value);
		return;
	}
	g = lookup_global(node->value);
	if (g) {
		/* an array and a struct both name storage so the value is its address */
		if (g->ty->kind == TY_ARRAY || g->ty->kind == TY_STRUCT)
			emit("\tleaq %s(%%rip), %%rax", node->value);
		else if (g->ty->kind == TY_PTR)
			emit("\tmovq %s(%%rip), %%rax", node->value);
		else
			emit("\tmovl %s(%%rip), %%eax", node->value);
		return;
	}
	v = lookup_var(node->value);
	if (v->ty->kind == TY_ARRAY || v->ty->kind == TY_STRUCT)
		emit("\tleaq %d(%%rbp), %%rax", v->offset);
	else
		emit_load(v->offset, v->ty);
}

static void gen_assign(struct ast_node *node)
{
	struct var_entry *v;
	struct glob_entry *g;

	gen_expression(node->children[0]);
	g = lookup_global(node->value);
	if (g) {
		if (g->ty->kind == TY_PTR)
			emit("\tmovq %%rax, %s(%%rip)", node->value);
		else
			emit("\tmovl %%eax, %s(%%rip)", node->value);
		return;
	}
	v = lookup_var(node->value);
	emit_store(v->offset, v->ty);
}

/* the storage an lvalue names lands in rax */
static void gen_lvalue_addr(struct ast_node *node)
{
	if (node->type == NODE_MEMBER || node->type == NODE_PTR_MEMBER)
		gen_member_addr(node);
	else
		gen_obj_addr(node);
}

static void gen_addr_of(struct ast_node *node)
{
	/* &a[i] and &s.f carry the operand as a child since there is no plain name to take */
	if (node->child_count > 0) {
		gen_lvalue_addr(node->children[0]);
		return;
	}
	if (is_global(node->value)) {
		emit("\tleaq %s(%%rip), %%rax", node->value);
		return;
	}
	emit("\tleaq %d(%%rbp), %%rax", lookup_var(node->value)->offset);
}

/* the width a dereference moves through 4 when the operand is not a pointer */
static int expr_deref_size(struct ast_node *node)
{
	struct type *ty = expr_type(node);

	return is_ptr_like(ty) ? type_size(ty->base) : 4;
}

static void gen_deref(struct ast_node *node)
{
	struct type *ty = expr_type(node);
	int esz = expr_deref_size(node->children[0]);

	gen_expression(node->children[0]);
	/* a partly indexed multi dim array is a row address so there is nothing to load yet */
	if (ty && ty->kind == TY_ARRAY)
		return;
	if (esz == 1)
		emit("\tmovsbl (%%rax), %%eax");
	else if (esz == 8)
		emit("\tmovq (%%rax), %%rax");
	else
		emit("\tmovl (%%rax), %%eax");
}

static void gen_deref_assign(struct ast_node *node)
{
	int esz = expr_deref_size(node->children[0]);

	gen_expression(node->children[1]); /* value in eax */
	emit("\tpush %%rax");
	gen_expression(node->children[0]); /* ptr in rax */
	emit("\tpop %%rcx");
	if (esz == 1)
		emit("\tmovb %%cl, (%%rax)");
	else if (esz == 8)
		emit("\tmovq %%rcx, (%%rax)");
	else
		emit("\tmovl %%ecx, (%%rax)");
}

/* rcx holds the address */
static void emit_load_at(struct type *ty)
{
	int esz = type_size(ty);

	if (ty->kind == TY_PTR || esz == 8)   emit("\tmovq (%%rcx), %%rax");
	else if (esz == 1 && ty->is_unsigned) emit("\tmovzbl (%%rcx), %%eax");
	else if (esz == 1)                    emit("\tmovsbl (%%rcx), %%eax");
	else                                  emit("\tmovl (%%rcx), %%eax");
}

/* rcx holds the address so the load and the mutation agree on where the target is */
static void emit_addr_inc_dec(struct ast_node *target, const char *op, int post)
{
	struct type *ty = expr_type(target);
	int esz;

	if (!ty) {
		fprintf(stderr, "codegen: not an lvalue\n");
		exit(1);
	}
	esz = type_size(ty);
	gen_lvalue_addr(target);
	emit("\tmovq %%rax, %%rcx");
	if (post)
		emit_load_at(ty);
	/* pointer steps by element size not 1 */
	if (ty->kind == TY_PTR)
		emit("\t%sq $%d, (%%rcx)", op, type_size(ty->base));
	else if (esz == 8) emit("\t%sq $1, (%%rcx)", op);
	else if (esz == 1) emit("\t%sb $1, (%%rcx)", op);
	else               emit("\t%sl $1, (%%rcx)", op);
	if (!post)
		emit_load_at(ty);
}

/* delta is +1 or -1
 * post means the old value loads before the mutation */
static void gen_inc_dec(struct ast_node *node, int delta, int post)
{
	struct var_entry *v;
	struct glob_entry *g;
	const char *op;
	int off;
	int esz;

	op = (delta > 0) ? "add" : "sub";
	if (node->child_count > 0) {
		emit_addr_inc_dec(node->children[0], op, post);
		return;
	}
	g = lookup_global(node->value);
	if (g) {
		if (g->ty->kind == TY_PTR) {
			/* pointer steps by element size not 1 */
			if (post)
				emit("\tmovq %s(%%rip), %%rax", node->value);
			emit("\t%sq $%d, %s(%%rip)", op, type_size(g->ty->base),
					node->value);
			if (!post)
				emit("\tmovq %s(%%rip), %%rax", node->value);
		} else {
			if (post)
				emit("\tmovl %s(%%rip), %%eax", node->value);
			emit("\t%sl $1, %s(%%rip)", op, node->value);
			if (!post)
				emit("\tmovl %s(%%rip), %%eax", node->value);
		}
		return;
	}
	v = lookup_var(node->value);
	off = v->offset;
	esz = type_size(v->ty);
	if (post)
		emit_load(off, v->ty);
	if (v->ty->kind == TY_PTR)
		emit("\t%sq $%d, %d(%%rbp)", op, type_size(v->ty->base), off);
	else if (esz == 8)     emit("\t%sq $1, %d(%%rbp)", op, off);
	else if (esz == 1)     emit("\t%sb $1, %d(%%rbp)", op, off);
	else                   emit("\t%sl $1, %d(%%rbp)", op, off);
	if (!post)
		emit_load(off, v->ty);
}

static void gen_fptr_call(struct ast_node *node, struct var_entry *fv)
{
	static const char *arg_regs[] = {
		"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
	};
	int i;
	int nargs;

	nargs = node->child_count;
	/* args go through the stack since evaluating a later arg could
	 * itself be a call that clobbers the arg regs */
	for (i = 0; i < nargs; i++) {
		gen_expression(node->children[i]);
		emit("\tpush %%rax");
	}
	for (i = nargs - 1; i >= 0; i--)
		emit("\tpop %s", arg_regs[i]);
	emit("\tmovq %d(%%rbp), %%rax", fv->offset);
	emit("\tcall *%%rax");
}

static int is_vararg_func(const char *name)
{
	int i;

	for (i = 0; i < vararg_count; i++)
		if (strcmp(vararg_names[i], name) == 0)
			return 1;
	return 0;
}

static void gen_va_start(struct ast_node *node)
{
	struct var_entry *v;

	v = lookup_var(node->value);
	emit("\tmovl $%d, %d(%%rbp)", current_va_gp_start, v->offset);
	/* parked past the save area so a float fetch takes the overflow path
	 * instead of reading junk since no xmm reg ever gets saved */
	emit("\tmovl $176, %d(%%rbp)", v->offset + 4);
	/* stack args start above the saved rbp and the return address */
	emit("\tleaq 16(%%rbp), %%rax");
	emit("\tmovq %%rax, %d(%%rbp)", v->offset + 8);
	emit("\tleaq %d(%%rbp), %%rax", current_va_save);
	emit("\tmovq %%rax, %d(%%rbp)", v->offset + 16);
}

/* fields at 0 gp_offset 4 fp_offset 8 overflow_arg_area 16 reg_save_area */
static void gen_va_arg(struct ast_node *node)
{
	struct var_entry *v;
	struct type *ty;
	int lbl;
	int wide;

	v = lookup_var(node->children[0]->value);
	lbl = label_count++;
	ty = expr_type(node);
	wide = ty->kind == TY_LONG || ty->kind == TY_PTR;

	emit("\tmovl %d(%%rbp), %%eax", v->offset);
	emit("\tcmpl $%d, %%eax", VA_SAVE_SIZE);
	emit("\tjae .Lvaover%d", lbl);
	/* movl into ecx zero extends so the byte count indexes the save area cleanly */
	emit("\tmovq %d(%%rbp), %%rdx", v->offset + 16);
	emit("\tmovl %%eax, %%ecx");
	emit("\taddq %%rcx, %%rdx");
	emit("\taddl $8, %%eax");
	emit("\tmovl %%eax, %d(%%rbp)", v->offset);
	emit("\tjmp .Lvaend%d", lbl);
	emit(".Lvaover%d:", lbl);
	emit("\tmovq %d(%%rbp), %%rdx", v->offset + 8);
	emit("\tleaq 8(%%rdx), %%rax");
	emit("\tmovq %%rax, %d(%%rbp)", v->offset + 8);
	emit(".Lvaend%d:", lbl);
	/* rdx holds the slot address and a char arrived already widened to int */
	if (wide)
		emit("\tmovq (%%rdx), %%rax");
	else
		emit("\tmovl (%%rdx), %%eax");
}

static void gen_push_arg(struct ast_node *arg, struct var_entry *sv, int nregs)
{
	if (nregs > 1) {
		/* struct > 8B: push HIGH first (deeper) so LOW ends up on top */
		emit("\tpushq %d(%%rbp)", sv->offset + 8);
		emit("\tpushq %d(%%rbp)", sv->offset);
	} else if (sv != NULL && sv->ty->kind == TY_STRUCT) {
		/* struct <= 8B fits in one reg: single qword push */
		emit("\tpushq %d(%%rbp)", sv->offset);
	} else {
		gen_expression(arg);
		emit("\tpush %%rax");
	}
}

static void gen_call(struct ast_node *node)
{
	static const char *arg_regs[] = {
		"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
	};
	struct var_entry *svs[16];
	int reg_base[16];
	int nregs[16];
	int i;
	int j;
	int nargs;
	int total_regs;
	int nstack;
	int pad;

	struct var_entry *fv;

	fv = try_lookup_var(node->value);
	if (fv && fv->ty->kind == TY_PTR && fv->ty->base->kind == TY_FUNC) {
		gen_fptr_call(node, fv);
		return;
	}

	nargs = node->child_count;
	total_regs = 0;
	for (i = 0; i < nargs && i < 16; i++) {
		svs[i] = NULL;
		nregs[i] = 1;
		if (node->children[i]->type == NODE_VAR) {
			svs[i] = try_lookup_var(node->children[i]->value);
			/* a struct array arg decays to a pointer so only real values go through regs */
			if (svs[i] && svs[i]->ty->kind == TY_STRUCT)
				nregs[i] = (type_size(svs[i]->ty) + 7) / 8;
		}
		reg_base[i] = total_regs;
		total_regs += nregs[i];
	}

	nstack = total_regs > 6 ? total_regs - 6 : 0;
	for (i = 0; i < nargs && nstack; i++)
		if (nregs[i] > 1 && reg_base[i] < 6 && reg_base[i] + nregs[i] > 6) {
			fprintf(stderr, "codegen: struct arg %d of '%s' straddles "
					"the register and stack halves\n", i, node->value);
			exit(1);
		}

	/* an odd number of stack args would leave rsp 8 off what the abi wants at call time */
	pad = (nstack % 2) ? 8 : 0;
	if (pad)
		emit("\tsubq $8, %%rsp");

	/* pushed last to first so the seventh arg ends up at the bottom of the block */
	for (i = nargs - 1; i >= 0; i--)
		if (reg_base[i] >= 6)
			gen_push_arg(node->children[i], svs[i], nregs[i]);

	/* reg args go through the stack too since evaluating a later arg could
	 * itself be a call that clobbers the arg regs */
	for (i = 0; i < nargs; i++)
		if (reg_base[i] < 6)
			gen_push_arg(node->children[i], svs[i], nregs[i]);
	/* pop right-to-left so arg0 ends up in rdi
	 * within each struct arg pop low bytes first (lowest reg) then high */
	for (i = nargs - 1; i >= 0; i--) {
		if (reg_base[i] >= 6)
			continue;
		for (j = 0; j < nregs[i]; j++)
			emit("\tpop %s", arg_regs[reg_base[i] + j]);
	}
	/* al tells a variadic callee how many vector regs carry args and redix never uses any */
	if (is_vararg_func(node->value))
		emit("\tmovl $0, %%eax");
	emit("\tcall %s", node->value);
	if (nstack || pad)
		emit("\taddq $%d, %%rsp", nstack * 8 + pad);
}

static void gen_string(struct ast_node *node)
{
	emit("\tleaq .LC%d(%%rip), %%rax", string_label(node->value));
}

/* truncation or extension to match the target type */
static void gen_cast(struct ast_node *node)
{
	struct type *ty = expr_type(node);

	gen_expression(node->children[0]);
	/* a pointer or a plain int is already the right width in the register */
	if (ty->kind == TY_CHAR)
		emit(ty->is_unsigned ? "\tmovzbl %%al, %%eax"
				: "\tmovsbl %%al, %%eax");
	else if (ty->kind == TY_LONG)
		emit("\tmovslq %%eax, %%rax");
}

/* like if/else but the chosen branch lands in eax */
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
	case NODE_MEMBER:
	case NODE_PTR_MEMBER:		gen_member_load(node);		break;
	case NODE_MEMBER_ASSIGN:
	case NODE_PTR_MEMBER_ASSIGN:	gen_member_store(node);		break;
	case NODE_CAST:			gen_cast(node);			break;
	case NODE_VA_ARG:		gen_va_arg(node);		break;
	case NODE_SIZEOF_STRUCT:
		emit("\tmov $%d, %%eax",
				struct_types[lookup_struct(node->value)].total_size);
		break;
	default:
		fprintf(stderr, "codegen: bad expression node type %d\n",
				node->type);
		exit(1);
	}
}

static void gen_return(struct ast_node *node)
{
	struct var_entry *v;
	int sidx;
	int sz;

	/* bare return in void functions has no expression child */
	if (node->child_count > 0) {
		if (current_func_sret_type[0]
				&& node->children[0]->type == NODE_VAR) {
			/* returning a struct var: pack bytes into rax and rdx per sysv */
			v = lookup_var(node->children[0]->value);
			sidx = lookup_struct(current_func_sret_type);
			sz = struct_types[sidx].total_size;
			emit("\tmovq %d(%%rbp), %%rax", v->offset);
			if (sz > 8)
				emit("\tmovq %d(%%rbp), %%rdx", v->offset + 8);
		} else {
			gen_expression(node->children[0]);
		}
	}
	emit("\tmovq %%rbp, %%rsp");
	emit("\tpopq %%rbp");
	emit("\tret");
}

static void gen_local_decl(struct ast_node *node)
{
	struct type *ty;
	struct type *ety;
	int offset;
	int i;

	ty = decl_type(node);
	offset = declare_var(decl_name(node), ty);

	switch (node->type) {
	case NODE_ARRAY_DECL:
	case NODE_CHAR_ARRAY_DECL:
	case NODE_PTR_ARRAY_DECL:
	case NODE_CHAR_PTR_ARRAY_DECL:
		/* the brace nesting is already flat so elements land at the innermost size */
		ety = elem_scalar(ty);
		for (i = 1; i < node->child_count; i++) {
			gen_expression(node->children[i]);
			emit_store(offset + (i - 1) * type_size(ety), ety);
		}
		break;
	case NODE_STRUCT_DECL:
		/* the only initializer a struct local takes is a struct returning call
		 * whose result arrives in rax:rdx */
		if (node->child_count > 1) {
			gen_expression(node->children[1]);
			emit("\tmovq %%rax, %d(%%rbp)", offset);
			if (type_size(ty) > 8)
				emit("\tmovq %%rdx, %d(%%rbp)", offset + 8);
		}
		break;
	/* the children of these carry the name and the length not a value */
	case NODE_STRUCT_PTR_DECL:
	case NODE_STRUCT_ARRAY_DECL:
	case NODE_VA_LIST_DECL:
		break;
	default:
		if (node->child_count > 0) {
			gen_expression(node->children[0]);
			/* an int sized result has to be widened to fill a 64 bit slot */
			if (ty->kind == TY_LONG && !expr_is_long(node->children[0]))
				emit("\tmovslq %%eax, %%rax");
			emit_store(offset, ty);
		}
		break;
	}
}

static void gen_compound(struct ast_node *node)
{
	int i;
	int saved;

	/* inner vars go out of scope when the block exits so outer names resolve again */
	saved = var_count;
	for (i = 0; i < node->child_count; i++)
		gen_statement(node->children[i]);
	var_count = saved;
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

static void gen_do_while(struct ast_node *node)
{
	int lbl = label_count++;
	char old_break[64];
	char old_cont[64];
	char start_label[64];
	char cont_label[64];
	char end_label[64];

	sprintf(start_label, ".Ldo_start%d", lbl);
	sprintf(cont_label, ".Ldo_cont%d", lbl);
	sprintf(end_label, ".Ldo_end%d", lbl);
	push_loop_labels(old_break, old_cont, end_label, cont_label);

	emit("%s:", start_label);
	gen_statement(node->children[0]);
	emit("%s:", cont_label);
	gen_expression(node->children[1]);
	emit("\tcmpl $0, %%eax");
	emit("\tjne %s", start_label);
	emit("%s:", end_label);

	pop_loop_labels(old_break, old_cont);
}

static void gen_switch(struct ast_node *node)
{
	int lbl = label_count++;
	int i;
	int j;
	int default_idx;
	int save_off;
	char old_break[64];
	char old_cont[64];
	char end_label[64];

	default_idx = -1;
	sprintf(end_label, ".Lswitch_end%d", lbl);
	push_loop_labels(old_break, old_cont, end_label, loop_cont_label);

	stack_offset -= 8;
	save_off = stack_offset;

	gen_expression(node->children[0]);
	emit("\tmovl %%eax, %d(%%rbp)", save_off);

	for (i = 1; i < node->child_count; i++) {
		if (node->children[i]->type == NODE_CASE) {
			emit("\tcmpl $%s, %d(%%rbp)", node->children[i]->value, save_off);
			emit("\tje .Lcase%d_%d", lbl, i);
		} else {
			default_idx = i;
		}
	}
	if (default_idx >= 0)
		emit("\tjmp .Ldefault%d", lbl);
	else
		emit("\tjmp %s", end_label);

	for (i = 1; i < node->child_count; i++) {
		if (node->children[i]->type == NODE_CASE)
			emit(".Lcase%d_%d:", lbl, i);
		else
			emit(".Ldefault%d:", lbl);
		for (j = 0; j < node->children[i]->child_count; j++)
			gen_statement(node->children[i]->children[j]);
	}

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

	gen_statement(node->children[0]);		/* init either expr or declaration */
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
	case NODE_DECLARATION:
	case NODE_CHAR_DECLARATION:
	case NODE_UNSIGNED_DECLARATION:
	case NODE_UNSIGNED_CHAR_DECLARATION:
	case NODE_LONG_DECLARATION:
	case NODE_PTR_DECLARATION:
	case NODE_CHAR_PTR_DECLARATION:
	case NODE_ARRAY_DECL:
	case NODE_CHAR_ARRAY_DECL:
	case NODE_PTR_ARRAY_DECL:
	case NODE_CHAR_PTR_ARRAY_DECL:
	case NODE_FPTR_DECLARATION:
	case NODE_STRUCT_DECL:
	case NODE_STRUCT_PTR_DECL:
	case NODE_STRUCT_ARRAY_DECL:
	case NODE_VA_LIST_DECL:		gen_local_decl(node);		break;
	case NODE_VA_START:		gen_va_start(node);		break;
	case NODE_VA_END:		break;
	case NODE_COMPOUND:		gen_compound(node);		break;
	case NODE_IF:			gen_if(node);			break;
	case NODE_WHILE:		gen_while(node);		break;
	case NODE_FOR:			gen_for(node);			break;
	case NODE_DO_WHILE:		gen_do_while(node);		break;
	case NODE_SWITCH:		gen_switch(node);		break;
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
	case NODE_PTR_MEMBER:
	case NODE_PTR_MEMBER_ASSIGN:
	case NODE_CAST:
	case NODE_VA_ARG:
	case NODE_SIZEOF_STRUCT:
		gen_expression(node);
		break;
	default:
		fprintf(stderr, "codegen: bad statement node type %d\n",
				node->type);
		exit(1);
	}
}

/* the body gets walked before codegen since the frame size must be known upfront */
static int count_stack_bytes(struct ast_node *node)
{
	struct type *ty;
	int i;
	int total = 0;

	ty = decl_type(node);
	if (ty)
		return slot_bytes(ty);
	/* the switch value needs a hidden slot to compare every case against */
	if (node->type == NODE_SWITCH)
		total += 8;
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
	struct ast_node *p;
	struct ast_node *body;
	struct type *pty;
	int i;
	int num_params;
	int num_locals;
	int alloc_size;
	int param_bytes;
	int offset;
	int has_sret;
	int param_start;
	int reg_idx;
	int sz;
	int is_variadic;

	/* stale locals from the previous function must not resolve here */
	var_count = 0;
	stack_offset = 0;
	loop_break_label[0] = '\0';
	loop_cont_label[0] = '\0';
	current_func_sret_type[0] = '\0';
	current_va_save = 0;
	current_va_gp_start = 0;

	/* both return markers sit ahead of the params so either one shifts where they start */
	param_start = node->child_count > 0
			&& (node->children[0]->type == NODE_STRUCT_RET
			|| node->children[0]->type == NODE_PTR_RET);
	has_sret = param_start && node->children[0]->type == NODE_STRUCT_RET;
	if (has_sret) {
		strncpy(current_func_sret_type, node->children[0]->value, 63);
		current_func_sret_type[63] = '\0';
	}

	/* body is always last child preceding children are params */
	body = node->children[node->child_count - 1];
	num_params = node->child_count - 1 - param_start;
	/* the ... marker is always the last param so dropping it leaves the named ones */
	is_variadic = num_params > 0
			&& node->children[param_start + num_params - 1]->type == NODE_VARARG;
	num_params -= is_variadic;

	emit(".global %s", node->value);
	emit("%s:", node->value);
	emit("\tpushq %%rbp");
	emit("\tmovq %%rsp, %%rbp");

	/* sum actual param sizes since struct val params can exceed 8 bytes */
	param_bytes = 0;
	for (i = 0; i < num_params; i++)
		param_bytes += slot_bytes(decl_type(node->children[param_start + i]));

	/* rounded up since the abi wants rsp 16 byte aligned at call time */
	num_locals = count_stack_bytes(body);
	alloc_size = param_bytes + num_locals;
	if (is_variadic)
		alloc_size += VA_SAVE_SIZE;
	if (alloc_size > 0) {
		if (alloc_size % 16 != 0)
			alloc_size += 16 - (alloc_size % 16);
		emit("\tsubq $%d, %%rsp", alloc_size);
	}

	/* all six get saved even the named ones since va_start indexes from gp_offset */
	if (is_variadic) {
		stack_offset -= VA_SAVE_SIZE;
		current_va_save = stack_offset;
		for (i = 0; i < 6; i++)
			emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[i],
					current_va_save + i * 8);
	}

	/* params get stack slots so they read and write like any other local */
	reg_idx = 0;
	for (i = 0; i < num_params && reg_idx < 6; i++) {
		p = node->children[param_start + i];
		pty = decl_type(p);
		offset = declare_var(decl_name(p), pty);
		sz = type_size(pty);
		if (pty->kind == TY_STRUCT) {
			emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[reg_idx], offset);
			if (sz > 8 && reg_idx + 1 < 6)
				emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[reg_idx + 1], offset + 8);
			reg_idx += (sz + 7) / 8;
			continue;
		}
		if (pty->kind == TY_PTR || sz == 8)
			emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[reg_idx], offset);
		else if (sz == 1)
			emit("\tmovb %s, %d(%%rbp)", byte_param_regs[reg_idx], offset);
		else
			emit("\tmovl %s, %d(%%rbp)", param_regs[reg_idx], offset);
		reg_idx++;
	}
	current_va_gp_start = reg_idx * 8;

	gen_statement(body);

	/* safety epilogue so void funcs that fall off the end still return
	 * if the last stmt was already a return this is just dead code */
	emit("\tmovl $0, %%eax");
	emit("\tmovq %%rbp, %%rsp");
	emit("\tpopq %%rbp");
	emit("\tret");
}

static int is_global_node(enum node_type t)
{
	return t == NODE_GLOBAL || t == NODE_GLOBAL_PTR
			|| t == NODE_GLOBAL_CHAR_PTR
			|| t == NODE_GLOBAL_ARRAY
			|| t == NODE_GLOBAL_CHAR_ARRAY
			|| t == NODE_GLOBAL_PTR_ARRAY
			|| t == NODE_GLOBAL_CHAR_PTR_ARRAY
			|| t == NODE_GLOBAL_STRUCT
			|| t == NODE_GLOBAL_STRUCT_PTR
			|| t == NODE_GLOBAL_STRUCT_ARRAY;
}

/* a pointer sitting in the data section can only name something the linker resolves */
static void emit_data_ptr(struct ast_node *node)
{
	struct glob_entry *g;

	if (node->type == NODE_STRING) {
		emit("\t.quad .LC%d", string_label(node->value));
		return;
	}
	if (node->type == NODE_ADDR_OF && node->child_count == 0) {
		emit("\t.quad %s", node->value);
		return;
	}
	if (node->type == NODE_VAR) {
		g = lookup_global(node->value);
		if ((g && g->ty->kind == TY_ARRAY) || is_func(node->value)) {
			emit("\t.quad %s", node->value);
			return;
		}
	}
	emit("\t.quad %d", const_eval(node));
}

/* elements past the ones given stay zero the way c wants */
static void emit_array_data(struct ast_node *node, int first, int total, int esz)
{
	int given = node->child_count - first;
	int i;

	if (given > total)
		given = total;
	for (i = 0; i < given; i++) {
		if (esz == 8)
			emit_data_ptr(node->children[first + i]);
		else if (esz == 1)
			emit("\t.byte %d", const_eval(node->children[first + i]));
		else
			emit("\t.long %d", const_eval(node->children[first + i]));
	}
	if (given < total)
		emit("\t.zero %d", (total - given) * esz);
}

/* the brace nesting is already flattened so the fields arrive in declaration order
 * and pos tracks where the last one ended so gaps become padding */
static void emit_struct_data(struct ast_node *node, int first, int count, int sidx)
{
	struct field_entry *f;
	int nf = struct_types[sidx].field_count;
	int given = node->child_count - first;
	int e;
	int i;
	int fsz;
	int pos;

	for (e = 0; e < count; e++) {
		pos = 0;
		for (i = 0; i < nf; i++) {
			f = &struct_flds[sidx][i];
			fsz = type_size(f->ty);
			if (f->offset > pos)
				emit("\t.zero %d", f->offset - pos);
			if (e * nf + i >= given)
				emit("\t.zero %d", fsz);
			else if (fsz == 8)
				emit_data_ptr(node->children[first + e * nf + i]);
			else if (fsz == 1)
				emit("\t.byte %d",
						const_eval(node->children[first + e * nf + i]));
			else
				emit("\t.long %d",
						const_eval(node->children[first + e * nf + i]));
			pos = f->offset + fsz;
		}
		if (struct_types[sidx].total_size > pos)
			emit("\t.zero %d", struct_types[sidx].total_size - pos);
	}
}

static void gen_global(struct ast_node *node)
{
	const char *name = decl_name(node);
	struct type *ty;
	int total;
	int first;
	int esz;
	int sidx;

	emit("\t.globl %s", name);
	switch (node->type) {
	case NODE_GLOBAL:
		emit("\t.align 4");
		emit("%s:", name);
		emit("\t.long %d",
				node->child_count > 0 ? const_eval(node->children[0]) : 0);
		break;
	case NODE_GLOBAL_PTR:
	case NODE_GLOBAL_CHAR_PTR:
	case NODE_GLOBAL_STRUCT_PTR:
		/* struct globals keep the name in a child so their initializer starts one later */
		first = node->type == NODE_GLOBAL_STRUCT_PTR ? 1 : 0;
		emit("\t.align 8");
		emit("%s:", name);
		if (node->child_count > first)
			emit_data_ptr(node->children[first]);
		else
			emit("\t.quad 0");
		break;
	case NODE_GLOBAL_STRUCT:
	case NODE_GLOBAL_STRUCT_ARRAY:
		sidx = lookup_struct(node->value);
		if (node->type == NODE_GLOBAL_STRUCT_ARRAY) {
			total = atoi(node->children[1]->value);
			first = 2;
		} else {
			total = 1;
			first = 1;
		}
		emit("\t.align 8");
		emit("%s:", name);
		if (node->child_count > first)
			emit_struct_data(node, first, total, sidx);
		else
			emit("\t.zero %d", struct_types[sidx].total_size * total);
		break;
	default:
		ty = decl_type(node);
		esz = type_size(elem_scalar(ty));
		total = type_size(ty) / esz;
		emit("\t.align 8");
		emit("%s:", name);
		if (node->child_count > 1)
			emit_array_data(node, 1, total, esz);
		else
			emit("\t.zero %d", total * esz);
		break;
	}
}

static void gen_program(struct ast_node *node)
{
	int i;
	int has_globals = 0;
	int sidx;

	collect_strings(node);

	sret_count = 0;
	func_count = 0;
	vararg_count = 0;
	retptr_count = 0;
	for (i = 0; i < node->child_count; i++)
		if (node->children[i]->type == NODE_STRUCT_DEF)
			gen_struct_def(node->children[i]);

	/* register function names so they can be used as values in expressions */
	for (i = 0; i < node->child_count; i++)
		if ((node->children[i]->type == NODE_FUNCTION
				|| node->children[i]->type == NODE_PROTO)
				&& func_count < MAX_FUNCS)
			func_names[func_count++] = node->children[i]->value;

	/* register variadic functions so call sites know to zero al */
	for (i = 0; i < node->child_count; i++) {
		struct ast_node *fn = node->children[i];
		int j;
		if (fn->type != NODE_FUNCTION && fn->type != NODE_PROTO)
			continue;
		for (j = 0; j < fn->child_count; j++)
			if (fn->children[j]->type == NODE_VARARG
					&& vararg_count < MAX_VARARG)
				vararg_names[vararg_count++] = fn->value;
	}

	/* register struct-returning functions so call sites can find them */
	for (i = 0; i < node->child_count; i++) {
		struct ast_node *fn = node->children[i];
		if ((fn->type == NODE_FUNCTION || fn->type == NODE_PROTO)
				&& fn->child_count > 0
				&& fn->children[0]->type == NODE_STRUCT_RET
				&& sret_count < MAX_SRET) {
			sidx = lookup_struct(fn->children[0]->value);
			strncpy(sret_tab[sret_count].name, fn->value, 63);
			sret_tab[sret_count].name[63] = '\0';
			sret_tab[sret_count].total_size = struct_types[sidx].total_size;
			sret_count++;
		}
	}

	/* register pointer returning functions so call sites can scale and find fields */
	for (i = 0; i < node->child_count; i++) {
		struct ast_node *fn = node->children[i];
		if ((fn->type == NODE_FUNCTION || fn->type == NODE_PROTO)
				&& fn->child_count > 0
				&& fn->children[0]->type == NODE_PTR_RET
				&& lookup_retptr(fn->value) < 0
				&& retptr_count < MAX_RETPTR)
			declare_retptr(fn->value, fn->children[0]->value,
					fn->children[0]->ptr_depth);
	}

	for (i = 0; i < node->child_count; i++)
		if (is_global_node(node->children[i]->type))
			declare_global(decl_name(node->children[i]),
					decl_type(node->children[i]));

	if (string_count > 0) {
		emit("\t.section .rodata");
		for (i = 0; i < string_count; i++) {
			emit(".LC%d:", i);
			emit("\t.string \"%s\"", string_lits[i]);
		}
	}

	for (i = 0; i < node->child_count; i++) {
		if (is_global_node(node->children[i]->type)) {
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
	init_types();
	gen_program(ast);
}
