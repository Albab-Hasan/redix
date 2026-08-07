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

#define MAX_VARS 128
#define MAX_DIMS 4
static struct var_entry {
	char *name;
	int offset;      /* negative offset from rbp */
	int is_ptr;
	int is_array;
	int is_struct;
	int is_unsigned;
	int is_fptr;     /* 8 byte slot holding a function address not a data pointer not a long */
	int elem_size;   /* 1 for char 4 for int 8 for long */
	int elem_ptr;    /* what an element points at when the elements are themselves pointers */
	int dims[MAX_DIMS];
	int ndims;       /* 0 when the name is not an array */
	char struct_type[64];
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
	int is_ptr;
	int is_array;
	int elem_size;
	int elem_ptr;
	int dims[MAX_DIMS];
	int ndims;
	char struct_type[64];
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
	int size;        /* 1 for char 4 for int 8 for long or any pointer */
	int is_ptr;
	int elem_size;   /* what a pointer field points at 0 when the field is not a pointer */
	char struct_type[64];
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
static int dim_list(struct ast_node *size, int *dims)
{
	int i;

	if (size->child_count + 1 > MAX_DIMS) {
		fprintf(stderr, "codegen: too many array dimensions\n");
		exit(1);
	}
	dims[0] = atoi(size->value);
	for (i = 0; i < size->child_count; i++)
		dims[i + 1] = atoi(size->children[i]->value);
	return size->child_count + 1;
}

static int dim_total(struct ast_node *size)
{
	int dims[MAX_DIMS];
	int nd = dim_list(size, dims);
	int total = 1;
	int i;

	for (i = 0; i < nd; i++)
		total *= dims[i];
	return total;
}

static void declare_global(const char *name, int is_ptr, int is_array,
		int elem_size)
{
	global_map[global_count].name = strdup(name);
	global_map[global_count].is_ptr = is_ptr;
	global_map[global_count].is_array = is_array;
	global_map[global_count].elem_size = elem_size;
	global_map[global_count].elem_ptr = 0;
	global_map[global_count].ndims = 0;
	global_map[global_count].struct_type[0] = '\0';
	global_count++;
}

static void declare_global_array(const char *name, struct ast_node *size,
		int elem_size, int elem_ptr)
{
	declare_global(name, 1, 1, elem_size);
	global_map[global_count - 1].elem_ptr = elem_ptr;
	global_map[global_count - 1].ndims =
			dim_list(size, global_map[global_count - 1].dims);
}

static void declare_global_struct(const char *name, const char *type_name,
		int is_ptr, int is_array)
{
	declare_global(name, is_ptr, is_array,
			struct_types[lookup_struct(type_name)].total_size);
	strncpy(global_map[global_count - 1].struct_type, type_name, 63);
	global_map[global_count - 1].struct_type[63] = '\0';
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

/* every scalar and ptr gets a full 8 byte slot so offsets never need alignment math */
static int declare_var(const char *name, int is_ptr, int is_unsigned, int elem_size)
{
	stack_offset -= 8;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].is_ptr = is_ptr;
	var_map[var_count].is_array = 0;
	var_map[var_count].is_struct = 0;
	var_map[var_count].is_unsigned = is_unsigned;
	var_map[var_count].is_fptr = 0;
	var_map[var_count].elem_size = elem_size;
	var_map[var_count].elem_ptr = 0;
	var_map[var_count].ndims = 0;
	var_map[var_count].struct_type[0] = '\0';
	var_count++;
	return stack_offset;
}

static int declare_fptr_var(const char *name)
{
	stack_offset -= 8;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].is_ptr = 0;
	var_map[var_count].is_array = 0;
	var_map[var_count].is_struct = 0;
	var_map[var_count].is_unsigned = 0;
	var_map[var_count].is_fptr = 1;
	var_map[var_count].elem_size = 8;
	var_map[var_count].elem_ptr = 0;
	var_map[var_count].ndims = 0;
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
	var_map[var_count].is_fptr = 0;
	var_map[var_count].elem_size = 4;
	var_map[var_count].elem_ptr = 0;
	var_map[var_count].ndims = 0;
	strncpy(var_map[var_count].struct_type, type_name, 63);
	var_map[var_count].struct_type[63] = '\0';
	var_count++;
	return stack_offset;
}

static int declare_struct_ptr_var(const char *name, const char *type_name)
{
	stack_offset -= 8;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].is_ptr = 1;
	var_map[var_count].is_array = 0;
	var_map[var_count].is_struct = 0;
	var_map[var_count].is_fptr = 0;
	/* stepping a struct pointer moves a whole struct not 4 bytes */
	var_map[var_count].elem_size =
			struct_types[lookup_struct(type_name)].total_size;
	var_map[var_count].elem_ptr = 0;
	var_map[var_count].ndims = 0;
	strncpy(var_map[var_count].struct_type, type_name, 63);
	var_map[var_count].struct_type[63] = '\0';
	var_count++;
	return stack_offset;
}

static int declare_struct_array_var(const char *name, const char *type_name,
		int count)
{
	int esz;
	int bytes;

	esz = struct_types[lookup_struct(type_name)].total_size;
	bytes = esz * count;
	if (bytes % 8 != 0)
		bytes += 8 - (bytes % 8);
	stack_offset -= bytes;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].is_ptr = 1;
	var_map[var_count].is_array = 1;
	var_map[var_count].is_struct = 1;
	var_map[var_count].is_unsigned = 0;
	var_map[var_count].is_fptr = 0;
	var_map[var_count].elem_size = esz;
	var_map[var_count].elem_ptr = 0;
	var_map[var_count].ndims = 0;
	strncpy(var_map[var_count].struct_type, type_name, 63);
	var_map[var_count].struct_type[63] = '\0';
	var_count++;
	return stack_offset;
}

/* padded to 8 so the slots placed after the array stay aligned */
static int declare_array(const char *name, int *dims, int ndims, int elem_size,
		int elem_ptr)
{
	int bytes = elem_size;
	int i;

	for (i = 0; i < ndims; i++)
		bytes *= dims[i];
	if (bytes % 8 != 0)
		bytes += 8 - (bytes % 8);
	stack_offset -= bytes;
	var_map[var_count].name = strdup(name);
	var_map[var_count].offset = stack_offset;
	var_map[var_count].is_ptr = 1;
	var_map[var_count].is_array = 1;
	var_map[var_count].is_struct = 0;
	var_map[var_count].is_fptr = 0;
	var_map[var_count].elem_size = elem_size;
	var_map[var_count].elem_ptr = elem_ptr;
	var_map[var_count].ndims = ndims;
	for (i = 0; i < ndims; i++)
		var_map[var_count].dims[i] = dims[i];
	var_map[var_count].struct_type[0] = '\0';
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
	const char *fname;
	int i;
	int idx;
	int off;
	int fsz;
	int fis_ptr;
	int felem;
	int max_align;

	idx = struct_type_count++;
	strncpy(struct_types[idx].name, node->value, 63);
	struct_types[idx].name[63] = '\0';
	struct_types[idx].field_count = node->child_count;
	off = 0;
	max_align = 1;
	for (i = 0; i < node->child_count; i++) {
		f = node->children[i];
		fname = f->value;
		fis_ptr = 0;
		felem = 0;
		struct_flds[idx][i].struct_type[0] = '\0';
		switch (f->type) {
		case NODE_CHAR_DECLARATION:
			fsz = 1;
			break;
		case NODE_LONG_DECLARATION:
			fsz = 8;
			break;
		case NODE_PTR_DECLARATION:
		case NODE_CHAR_PTR_DECLARATION:
			fsz = 8;
			fis_ptr = 1;
			felem = f->type == NODE_CHAR_PTR_DECLARATION ? 1 : 4;
			break;
		case NODE_STRUCT_PTR_DECL:
			fsz = 8;
			fis_ptr = 1;
			/* the pointee size stays unresolved here so a struct can point at itself */
			fname = f->children[0]->value;
			strncpy(struct_flds[idx][i].struct_type, f->value, 63);
			struct_flds[idx][i].struct_type[63] = '\0';
			break;
		default:
			fsz = 4;
			break;
		}
		/* char fields pack tight wider fields align to their size so loads never straddle */
		if (off % fsz != 0)
			off += fsz - (off % fsz);
		strncpy(struct_flds[idx][i].name, fname, 63);
		struct_flds[idx][i].name[63] = '\0';
		struct_flds[idx][i].offset = off;
		struct_flds[idx][i].size = fsz;
		struct_flds[idx][i].is_ptr = fis_ptr;
		struct_flds[idx][i].elem_size = felem;
		off += fsz;
		if (fsz > max_align)
			max_align = fsz;
	}
	/* total rounds up to the widest field so back to back structs would keep their fields aligned */
	if (off % max_align != 0)
		off += max_align - (off % max_align);
	struct_types[idx].total_size = off;
}

static void gen_struct_decl(struct ast_node *node)
{
	int offset;
	int sz;

	offset = declare_struct_var(node->children[0]->value, node->value);
	if (node->child_count > 1) {
		/* initializer must be a struct-returning call; result lands in rax:rdx */
		sz = struct_types[lookup_struct(node->value)].total_size;
		gen_expression(node->children[1]);
		emit("\tmovq %%rax, %d(%%rbp)", offset);
		if (sz > 8)
			emit("\tmovq %%rdx, %d(%%rbp)", offset + 8);
	}
}

static void gen_struct_ptr_decl(struct ast_node *node)
{
	declare_struct_ptr_var(node->children[0]->value, node->value);
}

static void gen_struct_array_decl(struct ast_node *node)
{
	declare_struct_array_var(node->children[0]->value, node->value,
			atoi(node->children[1]->value));
}

/* the struct type an expression denotes or NULL when it is not a struct */
static const char *expr_struct_type(struct ast_node *node)
{
	struct var_entry *v;
	struct glob_entry *g;
	struct field_entry *f;
	const char *t;

	switch (node->type) {
	case NODE_VAR:
		g = lookup_global(node->value);
		if (g)
			return g->struct_type[0] ? g->struct_type : NULL;
		v = try_lookup_var(node->value);
		if (v && v->struct_type[0])
			return v->struct_type;
		return NULL;
	case NODE_MEMBER:
	case NODE_MEMBER_ASSIGN:
	case NODE_PTR_MEMBER:
	case NODE_PTR_MEMBER_ASSIGN:
		t = expr_struct_type(node->children[0]);
		if (!t)
			return NULL;
		f = lookup_field(lookup_struct(t), node->value);
		return f->struct_type[0] ? f->struct_type : NULL;
	case NODE_DEREF:
		return expr_struct_type(node->children[0]);
	case NODE_BINARY:
		t = expr_struct_type(node->children[0]);
		return t ? t : expr_struct_type(node->children[1]);
	default:
		return NULL;
	}
}

/* the object is either a named variable or an a[i] element */
static void gen_obj_addr(struct ast_node *node)
{
	if (node->type == NODE_DEREF) {
		gen_expression(node->children[0]);
		return;
	}
	if (node->type != NODE_VAR) {
		fprintf(stderr, "codegen: member access on a non struct\n");
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
	const char *t;

	t = expr_struct_type(node->children[0]);
	if (!t) {
		fprintf(stderr, "codegen: no struct type for member '%s'\n",
				node->value);
		exit(1);
	}
	f = lookup_field(lookup_struct(t), node->value);
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
	struct field_entry *f = gen_member_addr(node);

	if (f->size == 8)      emit("\tmovq (%%rax), %%rax");
	else if (f->size == 1) emit("\tmovsbl (%%rax), %%eax");
	else                   emit("\tmovl (%%rax), %%eax");
}

static void gen_member_store(struct ast_node *node)
{
	struct field_entry *f;

	gen_expression(node->children[1]);
	emit("\tpush %%rax");
	f = gen_member_addr(node);
	emit("\tpop %%rcx");
	if (f->size == 8)      emit("\tmovq %%rcx, (%%rax)");
	else if (f->size == 1) emit("\tmovb %%cl, (%%rax)");
	else                   emit("\tmovl %%ecx, (%%rax)");
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

static int expr_is_unsigned(struct ast_node *node)
{
	struct var_entry *v;
	int i;

	switch (node->type) {
	case NODE_VAR:
		if (lookup_global(node->value))
			return 0;
		v = lookup_var(node->value);
		return v->is_unsigned;
	case NODE_BINARY:
	case NODE_UNARY:
		for (i = 0; i < node->child_count; i++)
			if (expr_is_unsigned(node->children[i])) return 1;
		return 0;
	case NODE_CAST:
		return strcmp(node->value, "unsigned") == 0
			|| strcmp(node->value, "unsigned_char") == 0;
	default:
		return 0;
	}
}

/* long means elem_size 8 and not a pointer */
static int expr_is_long(struct ast_node *node)
{
	struct var_entry *v;
	int i;

	switch (node->type) {
	case NODE_VAR:
		if (lookup_global(node->value))
			return 0;
		v = lookup_var(node->value);
		return v->elem_size == 8 && !v->is_ptr && !v->is_fptr;
	case NODE_BINARY:
	case NODE_UNARY:
		for (i = 0; i < node->child_count; i++)
			if (expr_is_long(node->children[i])) return 1;
		return 0;
	case NODE_CAST:
		return strcmp(node->value, "long") == 0;
	default:
		return 0;
	}
}

static int expr_deref_size(struct ast_node *node);

/* returns how many dims of an array expression are still unindexed
 * 0 means plain pointer rules apply */
static int expr_dims(struct ast_node *node, int *dims, int *esz)
{
	struct var_entry *v;
	struct glob_entry *g;
	int inner[MAX_DIMS];
	int inner_esz;
	int nd;
	int i;

	switch (node->type) {
	case NODE_VAR:
		g = lookup_global(node->value);
		if (g) {
			if (!g->ndims)
				return 0;
			for (i = 0; i < g->ndims; i++)
				dims[i] = g->dims[i];
			*esz = g->elem_size;
			return g->ndims;
		}
		v = try_lookup_var(node->value);
		if (!v || !v->ndims)
			return 0;
		for (i = 0; i < v->ndims; i++)
			dims[i] = v->dims[i];
		*esz = v->elem_size;
		return v->ndims;
	case NODE_DEREF:
		/* one index peels the outermost dim off and anything below stays an address */
		nd = expr_dims(node->children[0], inner, esz);
		if (nd < 2)
			return 0;
		for (i = 1; i < nd; i++)
			dims[i - 1] = inner[i];
		return nd - 1;
	case NODE_BINARY:
		if (node->value[1] != '\0')
			return 0;
		if (node->value[0] != '+' && node->value[0] != '-')
			return 0;
		nd = expr_dims(node->children[0], dims, esz);
		if (nd) {
			/* array minus array is an element count not an address */
			if (expr_dims(node->children[1], inner, &inner_esz))
				return 0;
			return nd;
		}
		return expr_dims(node->children[1], dims, esz);
	default:
		return 0;
	}
}

/* byte step of one index at this rank so every dim below the first times the element size */
static int expr_index_stride(struct ast_node *node)
{
	int dims[MAX_DIMS];
	int esz;
	int nd;
	int stride;
	int i;

	nd = expr_dims(node, dims, &esz);
	if (nd == 0)
		return 0;
	stride = esz;
	for (i = 1; i < nd; i++)
		stride *= dims[i];
	return stride;
}

/* what the elements of a pointer array point at 0 when the operand is not one */
static int expr_elem_ptr(struct ast_node *node)
{
	struct var_entry *v;
	struct glob_entry *g;
	int l;

	switch (node->type) {
	case NODE_VAR:
		g = lookup_global(node->value);
		if (g)
			return g->elem_ptr;
		v = try_lookup_var(node->value);
		return v ? v->elem_ptr : 0;
	case NODE_BINARY:
		if (node->value[1] != '\0')
			return 0;
		if (node->value[0] != '+' && node->value[0] != '-')
			return 0;
		l = expr_elem_ptr(node->children[0]);
		return l ? l : expr_elem_ptr(node->children[1]);
	case NODE_DEREF:
		/* peeling a dim off a multi dim pointer array leaves the same pointee */
		return expr_elem_ptr(node->children[0]);
	default:
		return 0;
	}
}

/* 4 for int* 1 for char* the struct size for struct* 0 if not a pointer */
static int expr_ptr_scale(struct ast_node *node)
{
	struct var_entry *v;
	struct glob_entry *g;
	struct field_entry *f;
	const char *t;
	int l;
	int r;
	int stride;

	/* a multi dim array steps a whole row so its stride beats the plain element size */
	stride = expr_index_stride(node);
	if (stride)
		return stride;

	switch (node->type) {
	case NODE_VAR:
		g = lookup_global(node->value);
		if (g)
			return g->is_ptr ? g->elem_size : 0;
		v = lookup_var(node->value);
		if (!v->is_ptr)
			return 0;
		return v->elem_size;
	case NODE_MEMBER:
	case NODE_PTR_MEMBER:
		t = expr_struct_type(node->children[0]);
		if (!t)
			return 0;
		f = lookup_field(lookup_struct(t), node->value);
		if (!f->is_ptr)
			return 0;
		if (f->struct_type[0])
			return struct_types[lookup_struct(f->struct_type)].total_size;
		return f->elem_size;
	case NODE_ADDR_OF:
		if (node->child_count > 0) {
			t = expr_struct_type(node->children[0]);
			if (t)
				return struct_types[lookup_struct(t)].total_size;
			if (node->children[0]->type == NODE_DEREF)
				return expr_deref_size(node->children[0]->children[0]);
			return 4;
		}
		g = lookup_global(node->value);
		if (g)
			return g->elem_size;
		return lookup_var(node->value)->elem_size;
	case NODE_DEREF:
		/* indexing a pointer array yields a pointer so its pointee sets the next scale */
		return expr_elem_ptr(node->children[0]);
	case NODE_CAST:
		if (strcmp(node->value, "int*") == 0)  return 4;
		if (strcmp(node->value, "char*") == 0) return 1;
		return 0;
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

static void emit_load(int off, int is_ptr, int is_unsigned, int esz)
{
	if (is_ptr)                        emit("\tmovq %d(%%rbp), %%rax", off);
	else if (esz == 8)                 emit("\tmovq %d(%%rbp), %%rax", off);
	else if (esz == 1 && is_unsigned)  emit("\tmovzbl %d(%%rbp), %%eax", off);
	else if (esz == 1)                 emit("\tmovsbl %d(%%rbp), %%eax", off);
	else                               emit("\tmovl %d(%%rbp), %%eax", off);
}

static void emit_store(int off, int is_ptr, int esz)
{
	if (is_ptr)        emit("\tmovq %%rax, %d(%%rbp)", off);
	else if (esz == 8) emit("\tmovq %%rax, %d(%%rbp)", off);
	else if (esz == 1) emit("\tmovb %%al, %d(%%rbp)", off);
	else               emit("\tmovl %%eax, %d(%%rbp)", off);
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
		if (g->is_array)
			emit("\tleaq %s(%%rip), %%rax", node->value);
		else if (g->is_ptr)
			emit("\tmovq %s(%%rip), %%rax", node->value);
		else
			emit("\tmovl %s(%%rip), %%eax", node->value);
		return;
	}
	v = lookup_var(node->value);
	if (v->is_array)
		emit("\tleaq %d(%%rbp), %%rax", v->offset);
	else
		emit_load(v->offset, v->is_ptr, v->is_unsigned, v->elem_size);
}

static void gen_assign(struct ast_node *node)
{
	struct var_entry *v;
	struct glob_entry *g;

	gen_expression(node->children[0]);
	g = lookup_global(node->value);
	if (g) {
		if (g->is_ptr && !g->is_array)
			emit("\tmovq %%rax, %s(%%rip)", node->value);
		else
			emit("\tmovl %%eax, %s(%%rip)", node->value);
		return;
	}
	v = lookup_var(node->value);
	emit_store(v->offset, v->is_ptr, v->elem_size);
}

static void gen_addr_of(struct ast_node *node)
{
	/* &a[i] and &s.f carry the operand as a child since there is no plain name to take */
	if (node->child_count > 0) {
		if (node->children[0]->type == NODE_MEMBER
				|| node->children[0]->type == NODE_PTR_MEMBER)
			gen_member_addr(node->children[0]);
		else
			gen_obj_addr(node->children[0]);
		return;
	}
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
	struct glob_entry *g;
	int scale;

	switch (node->type) {
	case NODE_VAR:
		g = lookup_global(node->value);
		if (g) {
			if (g->is_ptr)
				return g->elem_size;
			return 4;
		}
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
	int dims[MAX_DIMS];
	int desz;

	gen_expression(node->children[0]);
	/* a partly indexed multi dim array is a row address so there is nothing to load yet */
	if (expr_dims(node, dims, &desz) > 0)
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

static void gen_ptr_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 1, 0, 4);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovq %%rax, %d(%%rbp)", offset);
	}
}

static void gen_char_ptr_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 1, 0, 1);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovq %%rax, %d(%%rbp)", offset);
	}
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
	g = lookup_global(node->value);
	if (g) {
		if (g->is_ptr && !g->is_array) {
			/* pointer steps by element size not 1 */
			if (post)
				emit("\tmovq %s(%%rip), %%rax", node->value);
			emit("\t%sq $%d, %s(%%rip)", op, g->elem_size, node->value);
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
	esz = v->elem_size;
	if (post)
		emit_load(off, v->is_ptr, v->is_unsigned, esz);
	if (v->is_ptr)         emit("\t%sq $%d, %d(%%rbp)", op, esz, off);
	else if (esz == 8)     emit("\t%sq $1, %d(%%rbp)", op, off);
	else if (esz == 1)     emit("\t%sb $1, %d(%%rbp)", op, off);
	else                   emit("\t%sl $1, %d(%%rbp)", op, off);
	if (!post)
		emit_load(off, v->is_ptr, v->is_unsigned, esz);
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
	int sidx;

	struct var_entry *fv;

	fv = try_lookup_var(node->value);
	if (fv && fv->is_fptr) {
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
			if (svs[i] && svs[i]->is_struct && !svs[i]->is_array) {
				sidx = lookup_struct(svs[i]->struct_type);
				nregs[i] = (struct_types[sidx].total_size + 7) / 8;
			}
		}
		reg_base[i] = total_regs;
		total_regs += nregs[i];
	}

	/* args go through the stack since evaluating a later arg could
	 * itself be a call that clobbers the arg regs */
	for (i = 0; i < nargs; i++) {
		if (nregs[i] > 1) {
			/* struct > 8B: push HIGH first (deeper) so LOW ends up on top */
			emit("\tpushq %d(%%rbp)", svs[i]->offset + 8);
			emit("\tpushq %d(%%rbp)", svs[i]->offset);
		} else if (svs[i] != NULL && svs[i]->is_struct && !svs[i]->is_array) {
			/* struct <= 8B fits in one reg: single qword push */
			emit("\tpushq %d(%%rbp)", svs[i]->offset);
		} else {
			gen_expression(node->children[i]);
			emit("\tpush %%rax");
		}
	}
	/* pop right-to-left so arg0 ends up in rdi
	 * within each struct arg pop low bytes first (lowest reg) then high */
	for (i = nargs - 1; i >= 0; i--) {
		for (j = 0; j < nregs[i]; j++)
			emit("\tpop %s", arg_regs[reg_base[i] + j]);
	}
	emit("\tcall %s", node->value);
}

static void gen_string(struct ast_node *node)
{
	emit("\tleaq .LC%d(%%rip), %%rax", string_label(node->value));
}

/* truncation or extension to match the target type */
static void gen_cast(struct ast_node *node)
{
	const char *t = node->value;

	gen_expression(node->children[0]);
	if (strcmp(t, "char") == 0)
		emit("\tmovsbl %%al, %%eax");
	else if (strcmp(t, "unsigned_char") == 0)
		emit("\tmovzbl %%al, %%eax");
	else if (strcmp(t, "long") == 0)
		emit("\tmovslq %%eax, %%rax");
	/* int unsigned int* char* -- value already in the right register */
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

static void gen_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 0, 0, 4);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovl %%eax, %d(%%rbp)", offset);
	}
}

static void gen_char_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 0, 0, 1);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovb %%al, %d(%%rbp)", offset);
	}
}

static void gen_unsigned_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 0, 1, 4);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovl %%eax, %d(%%rbp)", offset);
	}
}

static void gen_unsigned_char_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 0, 1, 1);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovb %%al, %d(%%rbp)", offset);
	}
}

static void gen_long_declaration(struct ast_node *node)
{
	int offset = declare_var(node->value, 0, 0, 8);

	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		/* int-sized results need sign extension to fill the 64 bit slot */
		if (!expr_is_long(node->children[0]))
			emit("\tmovslq %%eax, %%rax");
		emit("\tmovq %%rax, %d(%%rbp)", offset);
	}
}

static void gen_array_decl(struct ast_node *node)
{
	int i;
	int base;
	int dims[MAX_DIMS];
	int nd;

	nd = dim_list(node->children[0], dims);
	base = declare_array(node->value, dims, nd, 4, 0);
	for (i = 1; i < node->child_count; i++) {
		gen_expression(node->children[i]);
		emit("\tmovl %%eax, %d(%%rbp)", base + (i - 1) * 4);
	}
}

static void gen_char_array_decl(struct ast_node *node)
{
	int i;
	int base;
	int dims[MAX_DIMS];
	int nd;

	nd = dim_list(node->children[0], dims);
	base = declare_array(node->value, dims, nd, 1, 0);
	for (i = 1; i < node->child_count; i++) {
		gen_expression(node->children[i]);
		emit("\tmovb %%al, %d(%%rbp)", base + (i - 1));
	}
}

/* elements are addresses so each slot is 8 bytes regardless of what it points at */
static void gen_ptr_array_decl(struct ast_node *node, int elem_ptr)
{
	int i;
	int base;
	int dims[MAX_DIMS];
	int nd;

	nd = dim_list(node->children[0], dims);
	base = declare_array(node->value, dims, nd, 8, elem_ptr);
	for (i = 1; i < node->child_count; i++) {
		gen_expression(node->children[i]);
		emit("\tmovq %%rax, %d(%%rbp)", base + (i - 1) * 8);
	}
}

static void gen_fptr_declaration(struct ast_node *node)
{
	int offset;

	offset = declare_fptr_var(node->value);
	if (node->child_count > 0) {
		gen_expression(node->children[0]);
		emit("\tmovq %%rax, %d(%%rbp)", offset);
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
	case NODE_DECLARATION:			gen_declaration(node);			break;
	case NODE_CHAR_DECLARATION:		gen_char_declaration(node);		break;
	case NODE_UNSIGNED_DECLARATION:		gen_unsigned_declaration(node);		break;
	case NODE_UNSIGNED_CHAR_DECLARATION:	gen_unsigned_char_declaration(node);	break;
	case NODE_LONG_DECLARATION:		gen_long_declaration(node);		break;
	case NODE_PTR_DECLARATION:		gen_ptr_declaration(node);		break;
	case NODE_CHAR_PTR_DECLARATION:		gen_char_ptr_declaration(node);		break;
	case NODE_ARRAY_DECL:		gen_array_decl(node);		break;
	case NODE_CHAR_ARRAY_DECL:	gen_char_array_decl(node);	break;
	case NODE_PTR_ARRAY_DECL:	gen_ptr_array_decl(node, 4);	break;
	case NODE_CHAR_PTR_ARRAY_DECL:	gen_ptr_array_decl(node, 1);	break;
	case NODE_FPTR_DECLARATION:	gen_fptr_declaration(node);	break;
	case NODE_STRUCT_DECL:		gen_struct_decl(node);		break;
	case NODE_STRUCT_PTR_DECL:	gen_struct_ptr_decl(node);	break;
	case NODE_STRUCT_ARRAY_DECL:	gen_struct_array_decl(node);	break;
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
	int i;
	int total = 0;
	int n;
	int bytes;
	int sidx;

	if (node->type == NODE_DECLARATION
			|| node->type == NODE_PTR_DECLARATION
			|| node->type == NODE_CHAR_DECLARATION
			|| node->type == NODE_CHAR_PTR_DECLARATION
			|| node->type == NODE_UNSIGNED_DECLARATION
			|| node->type == NODE_UNSIGNED_CHAR_DECLARATION
			|| node->type == NODE_LONG_DECLARATION
			|| node->type == NODE_FPTR_DECLARATION)
		return 8;
	if (node->type == NODE_ARRAY_DECL) {
		n = dim_total(node->children[0]);
		bytes = n * 4;
		if (bytes % 8 != 0)
			bytes += 8 - (bytes % 8);
		return bytes;
	}
	if (node->type == NODE_CHAR_ARRAY_DECL) {
		n = dim_total(node->children[0]);
		bytes = n * 1;
		if (bytes % 8 != 0)
			bytes += 8 - (bytes % 8);
		return bytes;
	}
	if (node->type == NODE_PTR_ARRAY_DECL
			|| node->type == NODE_CHAR_PTR_ARRAY_DECL)
		return dim_total(node->children[0]) * 8;
	if (node->type == NODE_STRUCT_PTR_DECL)
		return 8;
	if (node->type == NODE_STRUCT_DECL || node->type == NODE_STRUCT_ARRAY_DECL) {
		sidx = lookup_struct(node->value);
		bytes = struct_types[sidx].total_size;
		if (node->type == NODE_STRUCT_ARRAY_DECL)
			bytes *= atoi(node->children[1]->value);
		if (bytes % 8 != 0)
			bytes += 8 - (bytes % 8);
		return bytes;
	}
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
	int i;
	int num_params;
	int num_locals;
	int alloc_size;
	int param_bytes;
	int offset;
	int is_ptr_param;
	int is_char_param;
	int is_unsigned;
	int elem_sz;
	int has_sret;
	int param_start;
	int reg_idx;
	int sidx;
	int sz;

	/* stale locals from the previous function must not resolve here */
	var_count = 0;
	stack_offset = 0;
	loop_break_label[0] = '\0';
	loop_cont_label[0] = '\0';
	current_func_sret_type[0] = '\0';

	has_sret = node->child_count > 0
			&& node->children[0]->type == NODE_STRUCT_RET;
	param_start = has_sret ? 1 : 0;
	if (has_sret) {
		strncpy(current_func_sret_type, node->children[0]->value, 63);
		current_func_sret_type[63] = '\0';
	}

	/* body is always last child preceding children are params */
	body = node->children[node->child_count - 1];
	num_params = node->child_count - 1 - param_start;

	emit(".global %s", node->value);
	emit("%s:", node->value);
	emit("\tpushq %%rbp");
	emit("\tmovq %%rsp, %%rbp");

	/* sum actual param sizes since struct val params can exceed 8 bytes */
	param_bytes = 0;
	for (i = 0; i < num_params; i++) {
		p = node->children[param_start + i];
		if (p->type == NODE_STRUCT_VAL_PARAM) {
			sidx = lookup_struct(p->value);
			sz = struct_types[sidx].total_size;
			if (sz % 8 != 0)
				sz += 8 - (sz % 8);
			param_bytes += sz;
		} else {
			param_bytes += 8;
		}
	}

	/* rounded up since the abi wants rsp 16 byte aligned at call time */
	num_locals = count_stack_bytes(body);
	alloc_size = param_bytes + num_locals;
	if (alloc_size > 0) {
		if (alloc_size % 16 != 0)
			alloc_size += 16 - (alloc_size % 16);
		emit("\tsubq $%d, %%rsp", alloc_size);
	}

	/* params get stack slots so they read and write like any other local */
	reg_idx = 0;
	for (i = 0; i < num_params && reg_idx < 6; i++) {
		p = node->children[param_start + i];
		if (p->type == NODE_STRUCT_VAL_PARAM) {
			sidx = lookup_struct(p->value);
			sz = struct_types[sidx].total_size;
			offset = declare_struct_var(p->children[0]->value, p->value);
			emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[reg_idx], offset);
			if (sz > 8 && reg_idx + 1 < 6)
				emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[reg_idx + 1], offset + 8);
			reg_idx += (sz + 7) / 8;
			continue;
		}
		if (p->type == NODE_STRUCT_PTR_DECL) {
			offset = declare_struct_ptr_var(
					p->children[0]->value, p->value);
			emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[reg_idx], offset);
			reg_idx++;
			continue;
		}
		if (p->type == NODE_FPTR_DECLARATION) {
			offset = declare_fptr_var(p->value);
			emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[reg_idx], offset);
			reg_idx++;
			continue;
		}
		is_ptr_param = p->type == NODE_PTR_DECLARATION
				|| p->type == NODE_CHAR_PTR_DECLARATION;
		is_char_param = p->type == NODE_CHAR_DECLARATION
				|| p->type == NODE_CHAR_PTR_DECLARATION
				|| p->type == NODE_UNSIGNED_CHAR_DECLARATION;
		is_unsigned = p->type == NODE_UNSIGNED_DECLARATION
				|| p->type == NODE_UNSIGNED_CHAR_DECLARATION;
		if (p->type == NODE_LONG_DECLARATION)
			elem_sz = 8;
		else
			elem_sz = is_char_param ? 1 : 4;
		offset = declare_var(p->value, is_ptr_param, is_unsigned, elem_sz);
		if (is_ptr_param || elem_sz == 8)
			emit("\tmovq %s, %d(%%rbp)", ptr_param_regs[reg_idx], offset);
		else if (is_char_param)
			emit("\tmovb %s, %d(%%rbp)", byte_param_regs[reg_idx], offset);
		else
			emit("\tmovl %s, %d(%%rbp)", param_regs[reg_idx], offset);
		reg_idx++;
	}

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

/* struct globals keep the type in value so the label comes from the child */
static const char *global_label(struct ast_node *node)
{
	if (node->type == NODE_GLOBAL_STRUCT
			|| node->type == NODE_GLOBAL_STRUCT_PTR
			|| node->type == NODE_GLOBAL_STRUCT_ARRAY)
		return node->children[0]->value;
	return node->value;
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
		if ((g && g->is_array) || is_func(node->value)) {
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
	int pos;

	for (e = 0; e < count; e++) {
		pos = 0;
		for (i = 0; i < nf; i++) {
			f = &struct_flds[sidx][i];
			if (f->offset > pos)
				emit("\t.zero %d", f->offset - pos);
			if (e * nf + i >= given)
				emit("\t.zero %d", f->size);
			else if (f->size == 8)
				emit_data_ptr(node->children[first + e * nf + i]);
			else if (f->size == 1)
				emit("\t.byte %d",
						const_eval(node->children[first + e * nf + i]));
			else
				emit("\t.long %d",
						const_eval(node->children[first + e * nf + i]));
			pos = f->offset + f->size;
		}
		if (struct_types[sidx].total_size > pos)
			emit("\t.zero %d", struct_types[sidx].total_size - pos);
	}
}

static void gen_global(struct ast_node *node)
{
	const char *name = global_label(node);
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
		total = dim_total(node->children[0]);
		if (node->type == NODE_GLOBAL_ARRAY)
			esz = 4;
		else if (node->type == NODE_GLOBAL_CHAR_ARRAY)
			esz = 1;
		else
			esz = 8;
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
	for (i = 0; i < node->child_count; i++)
		if (node->children[i]->type == NODE_STRUCT_DEF)
			gen_struct_def(node->children[i]);

	/* register function names so they can be used as values in expressions */
	for (i = 0; i < node->child_count; i++)
		if ((node->children[i]->type == NODE_FUNCTION
				|| node->children[i]->type == NODE_PROTO)
				&& func_count < MAX_FUNCS)
			func_names[func_count++] = node->children[i]->value;

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

	for (i = 0; i < node->child_count; i++) {
		switch (node->children[i]->type) {
		case NODE_GLOBAL:
			declare_global(node->children[i]->value, 0, 0, 4);
			break;
		case NODE_GLOBAL_PTR:
			declare_global(node->children[i]->value, 1, 0, 4);
			break;
		case NODE_GLOBAL_CHAR_PTR:
			declare_global(node->children[i]->value, 1, 0, 1);
			break;
		case NODE_GLOBAL_ARRAY:
			declare_global_array(node->children[i]->value,
					node->children[i]->children[0], 4, 0);
			break;
		case NODE_GLOBAL_CHAR_ARRAY:
			declare_global_array(node->children[i]->value,
					node->children[i]->children[0], 1, 0);
			break;
		case NODE_GLOBAL_PTR_ARRAY:
			declare_global_array(node->children[i]->value,
					node->children[i]->children[0], 8, 4);
			break;
		case NODE_GLOBAL_CHAR_PTR_ARRAY:
			declare_global_array(node->children[i]->value,
					node->children[i]->children[0], 8, 1);
			break;
		case NODE_GLOBAL_STRUCT:
			declare_global_struct(node->children[i]->children[0]->value,
					node->children[i]->value, 0, 0);
			break;
		case NODE_GLOBAL_STRUCT_PTR:
			declare_global_struct(node->children[i]->children[0]->value,
					node->children[i]->value, 1, 0);
			break;
		case NODE_GLOBAL_STRUCT_ARRAY:
			declare_global_struct(node->children[i]->children[0]->value,
					node->children[i]->value, 1, 1);
			break;
		default:
			break;
		}
	}

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
	gen_program(ast);
}
