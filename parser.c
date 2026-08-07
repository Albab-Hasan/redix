#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"

static int position;
static struct token *tokens;
static int token_count;

static struct token *current(void)
{
	return &tokens[position];
}

/* dies on a mismatch so parse functions never need an error path */
static struct token *expect(enum token_type type)
{
	if (position >= token_count || current()->type != type) {
		fprintf(stderr, "parser: unexpected token '%s'\n",
				current()->value);
		exit(1);
	}
	return &tokens[position++];
}

static struct ast_node *make_node(enum node_type type, char *value)
{
	struct ast_node *node = malloc(sizeof(struct ast_node));
	node->type = type;
	node->value = value ? strdup(value) : NULL;
	node->child_count = 0;
	node->children = NULL;
	return node;
}

static void add_child(struct ast_node *parent, struct ast_node *child)
{
	parent->child_count++;
	parent->children = realloc(parent->children,
			parent->child_count * sizeof(struct ast_node *));
	parent->children[parent->child_count - 1] = child;
}

/* enum constants get folded to plain numbers at parse time like sizeof
 * so codegen never sees them */
struct enum_entry {
	char *name;
	int value;
};

#define MAX_ENUMS 64

static struct enum_entry enum_map[MAX_ENUMS];
static int enum_count;

static struct enum_entry *lookup_enum(const char *name)
{
	int i;

	for (i = 0; i < enum_count; i++) {
		if (strcmp(enum_map[i].name, name) == 0)
			return &enum_map[i];
	}
	return NULL;
}

static struct ast_node *parse_statement(void);
static struct ast_node *parse_expression(void);

static struct ast_node *parse_primary(void)
{
	struct token *tok;
	struct token *field;
	struct ast_node *node;
	struct ast_node *base;
	struct ast_node *binary;
	struct enum_entry *ent;
	int arrow;
	char buf[16];

	if (current()->type == TOKEN_STRING_LITERAL) {
		tok = &tokens[position++];
		return make_node(NODE_STRING, tok->value);
	}

	if (current()->type == TOKEN_NUMBER) {
		tok = &tokens[position++];
		return make_node(NODE_NUMBER, tok->value);
	}

	if (current()->type == TOKEN_IDENTIFIER) {
		tok = &tokens[position++];
		ent = lookup_enum(tok->value);
		if (ent) {
			sprintf(buf, "%d", ent->value);
			return make_node(NODE_NUMBER, buf);
		}
		if (current()->type == TOKEN_LPAREN) {
			position++;
			node = make_node(NODE_CALL, tok->value);
			while (current()->type != TOKEN_RPAREN) {
				add_child(node, parse_expression());
				if (current()->type == TOKEN_COMMA)
					position++;
			}
			expect(TOKEN_RPAREN);
			return node;
		}
		if (current()->type == TOKEN_INC) {
			position++;
			return make_node(NODE_POSTFIX_INC, tok->value);
		}
		if (current()->type == TOKEN_DEC) {
			position++;
			return make_node(NODE_POSTFIX_DEC, tok->value);
		}
		/* a postfix chain so p->next->val and a[i].x keep building on the last result */
		node = make_node(NODE_VAR, tok->value);
		while (current()->type == TOKEN_LBRACKET
				|| current()->type == TOKEN_DOT
				|| current()->type == TOKEN_ARROW) {
			if (current()->type == TOKEN_LBRACKET) {
				/* p[i] is sugar for *(p + i) */
				position++;
				binary = make_node(NODE_BINARY, "+");
				add_child(binary, node);
				add_child(binary, parse_expression());
				expect(TOKEN_RBRACKET);
				node = make_node(NODE_DEREF, NULL);
				add_child(node, binary);
				continue;
			}
			arrow = current()->type == TOKEN_ARROW;
			position++;
			field = expect(TOKEN_IDENTIFIER);
			base = node;
			node = make_node(arrow ? NODE_PTR_MEMBER : NODE_MEMBER,
					field->value);
			add_child(node, base);
		}
		return node;
	}

	if (current()->type == TOKEN_LPAREN) {
		position++;
		node = parse_expression();
		expect(TOKEN_RPAREN);
		return node;
	}

	fprintf(stderr, "parser: expected number or variable got '%s'\n",
			current()->value);
	exit(1);
	return NULL;
}

static struct ast_node *parse_unary(void)
{
	struct token *tok;
	struct ast_node *node;
	struct ast_node *addr;

	if (current()->type == TOKEN_MINUS ||
			current()->type == TOKEN_TILDE ||
			current()->type == TOKEN_BANG) {
		tok = &tokens[position++];
		node = make_node(NODE_UNARY, tok->value);
		add_child(node, parse_unary());
		return node;
	}
	if (current()->type == TOKEN_INC || current()->type == TOKEN_DEC) {
		enum token_type op = current()->type;
		position++;
		tok = expect(TOKEN_IDENTIFIER);
		return make_node(op == TOKEN_INC ? NODE_PREFIX_INC : NODE_PREFIX_DEC,
				tok->value);
	}
	if (current()->type == TOKEN_AMPERSAND) {
		position++;
		node = parse_unary();
		/* a plain name keeps the name in value so nothing downstream has to walk a child */
		if (node->type == NODE_VAR) {
			addr = make_node(NODE_ADDR_OF, node->value);
			free_ast(node);
			return addr;
		}
		addr = make_node(NODE_ADDR_OF, NULL);
		add_child(addr, node);
		return addr;
	}
	if (current()->type == TOKEN_STAR) {
		position++;
		node = make_node(NODE_DEREF, NULL);
		add_child(node, parse_unary());
		return node;
	}
	if (current()->type == TOKEN_SIZEOF) {
		int sz;
		char buf[8];

		position++;
		expect(TOKEN_LPAREN);
		if (current()->type == TOKEN_STRUCT) {
			position++;
			tok = expect(TOKEN_IDENTIFIER);
			if (current()->type == TOKEN_STAR) {
				position++;
				expect(TOKEN_RPAREN);
				return make_node(NODE_NUMBER, "8");
			}
			expect(TOKEN_RPAREN);
			/* the layout only exists in codegen so the size resolves there */
			return make_node(NODE_SIZEOF_STRUCT, tok->value);
		}
		if (current()->type == TOKEN_INT || current()->type == TOKEN_CHAR) {
			sz = (current()->type == TOKEN_CHAR) ? 1 : 4;
			position++;
			if (current()->type == TOKEN_STAR) {
				position++;
				sz = 8;
			}
		} else {
			fprintf(stderr, "parser: sizeof expects a type\n");
			exit(1);
			sz = 0;
		}
		expect(TOKEN_RPAREN);
		sprintf(buf, "%d", sz);
		return make_node(NODE_NUMBER, buf);
	}
	/* peek at position+1 to tell (type) cast from (expr) grouping */
	if (current()->type == TOKEN_LPAREN
			&& position + 1 < token_count
			&& (tokens[position + 1].type == TOKEN_INT
				|| tokens[position + 1].type == TOKEN_CHAR
				|| tokens[position + 1].type == TOKEN_LONG
				|| tokens[position + 1].type == TOKEN_UNSIGNED)) {
		struct ast_node *node;
		const char *cast_type;
		int is_char;
		int is_long;
		int is_unsigned;
		int is_ptr;

		position++; /* consume ( */
		is_char = 0;
		is_long = 0;
		is_unsigned = 0;
		is_ptr = 0;
		if (current()->type == TOKEN_UNSIGNED) {
			is_unsigned = 1;
			position++;
		}
		if (current()->type == TOKEN_LONG) {
			is_long = 1;
			position++;
			if (current()->type == TOKEN_INT)
				position++;
		} else if (current()->type == TOKEN_INT) {
			position++;
		} else if (current()->type == TOKEN_CHAR) {
			is_char = 1;
			position++;
		}
		if (current()->type == TOKEN_STAR) {
			is_ptr = 1;
			position++;
		}
		expect(TOKEN_RPAREN);
		if      (is_ptr && is_char)        cast_type = "char*";
		else if (is_ptr)                   cast_type = "int*";
		else if (is_long)                  cast_type = "long";
		else if (is_unsigned && is_char)   cast_type = "unsigned_char";
		else if (is_unsigned)              cast_type = "unsigned";
		else if (is_char)                  cast_type = "char";
		else                               cast_type = "int";
		node = make_node(NODE_CAST, (char *)cast_type);
		add_child(node, parse_unary());
		return node;
	}
	return parse_primary();
}

/* matchfn says which tokens are operators at this level
 * next parses the tighter precedence level below */
static struct ast_node *parse_binop(struct ast_node *(*next)(void),
		int (*matchfn)(enum token_type))
{
	struct ast_node *left;
	struct ast_node *node;
	struct token *op;

	left = next();
	while (matchfn(current()->type)) {
		op = &tokens[position++];
		node = make_node(NODE_BINARY, op->value);
		add_child(node, left);
		add_child(node, next());
		left = node;
	}
	return left;
}

static int is_mul(enum token_type t)
{
	return t == TOKEN_STAR || t == TOKEN_SLASH || t == TOKEN_PERCENT;
}

static int is_add(enum token_type t)
{
	return t == TOKEN_PLUS || t == TOKEN_MINUS;
}

static int is_shift(enum token_type t)
{
	return t == TOKEN_LSHIFT || t == TOKEN_RSHIFT;
}

static int is_rel(enum token_type t)
{
	return t == TOKEN_LT || t == TOKEN_GT
			|| t == TOKEN_LTE || t == TOKEN_GTE;
}

static int is_eq(enum token_type t)
{
	return t == TOKEN_EQ || t == TOKEN_NEQ;
}

static int is_bw_and(enum token_type t)
{
	return t == TOKEN_AMPERSAND;
}

static int is_bw_xor(enum token_type t)
{
	return t == TOKEN_CARET;
}

static int is_bw_or(enum token_type t)
{
	return t == TOKEN_PIPE;
}

static int is_and(enum token_type t)
{
	return t == TOKEN_AND;
}

static int is_or(enum token_type t)
{
	return t == TOKEN_OR;
}

static struct ast_node *parse_multiplicative(void)
{
	return parse_binop(parse_unary, is_mul);
}

static struct ast_node *parse_additive(void)
{
	return parse_binop(parse_multiplicative, is_add);
}

static struct ast_node *parse_shift(void)
{
	return parse_binop(parse_additive, is_shift);
}

static struct ast_node *parse_relational(void)
{
	return parse_binop(parse_shift, is_rel);
}

static struct ast_node *parse_equality(void)
{
	return parse_binop(parse_relational, is_eq);
}

static struct ast_node *parse_bitwise_and(void)
{
	return parse_binop(parse_equality, is_bw_and);
}

static struct ast_node *parse_bitwise_xor(void)
{
	return parse_binop(parse_bitwise_and, is_bw_xor);
}

static struct ast_node *parse_bitwise_or(void)
{
	return parse_binop(parse_bitwise_xor, is_bw_or);
}

static struct ast_node *parse_logical_and(void)
{
	return parse_binop(parse_bitwise_or, is_and);
}

static struct ast_node *parse_logical_or(void)
{
	return parse_binop(parse_logical_and, is_or);
}

/* false branch is right associative */
static struct ast_node *parse_ternary(void)
{
	struct ast_node *cond;
	struct ast_node *node;

	cond = parse_logical_or();

	if (current()->type == TOKEN_QUESTION) {
		position++;
		node = make_node(NODE_TERNARY, NULL);
		add_child(node, cond);
		add_child(node, parse_expression());
		expect(TOKEN_COLON);
		add_child(node, parse_ternary());
		return node;
	}

	return cond;
}

static struct ast_node *parse_expression(void)
{
	struct ast_node *left;
	struct ast_node *node;
	struct ast_node *binary;
	struct ast_node *ptr_expr;
	struct ast_node *var_node;
	const char *op;
	char *fname;

	left = parse_ternary();

	if (current()->type == TOKEN_ASSIGN) {
		position++;
		if (left->type == NODE_DEREF) {
			/* free_ast on the shell would take the ptr child with it so the frees are manual */
			ptr_expr = left->children[0];
			free(left->children);
			free(left->value);
			free(left);
			node = make_node(NODE_DEREF_ASSIGN, NULL);
			add_child(node, ptr_expr);
			add_child(node, parse_expression());
			return node;
		}
		if (left->type == NODE_MEMBER) {
			fname = strdup(left->value);
			var_node = left->children[0];
			free(left->children);
			free(left->value);
			free(left);
			node = make_node(NODE_MEMBER_ASSIGN, fname);
			free(fname);
			add_child(node, var_node);
			add_child(node, parse_expression());
			return node;
		}
		if (left->type == NODE_PTR_MEMBER) {
			fname = strdup(left->value);
			var_node = left->children[0];
			free(left->children);
			free(left->value);
			free(left);
			node = make_node(NODE_PTR_MEMBER_ASSIGN, fname);
			free(fname);
			add_child(node, var_node);
			add_child(node, parse_expression());
			return node;
		}
		node = make_node(NODE_ASSIGN, left->value);
		free_ast(left);
		add_child(node, parse_expression()); /* right associative */
		return node;
	}

	/* desugar x op= e into x = x op e */
	op = NULL;
	if      (current()->type == TOKEN_PLUS_ASSIGN)  op = "+";
	else if (current()->type == TOKEN_MINUS_ASSIGN) op = "-";
	else if (current()->type == TOKEN_STAR_ASSIGN)  op = "*";
	else if (current()->type == TOKEN_SLASH_ASSIGN) op = "/";

	if (op) {
		position++;
		node = make_node(NODE_ASSIGN, left->value);
		binary = make_node(NODE_BINARY, (char *)op);
		add_child(binary, make_node(NODE_VAR, node->value));
		free_ast(left);
		add_child(binary, parse_expression());
		add_child(node, binary);
		return node;
	}

	return left;
}

static struct ast_node *parse_block(void)
{
	struct ast_node *node;

	expect(TOKEN_LBRACE);
	node = make_node(NODE_COMPOUND, NULL);
	while (current()->type != TOKEN_RBRACE)
		add_child(node, parse_statement());
	expect(TOKEN_RBRACE);
	return node;
}

static struct ast_node *parse_return(void)
{
	struct ast_node *node;

	position++;
	node = make_node(NODE_RETURN, NULL);
	/* bare return for void functions */
	if (current()->type != TOKEN_SEMICOLON)
		add_child(node, parse_expression());
	expect(TOKEN_SEMICOLON);
	return node;
}

/* extra dims hang off the first size node since the children of a declaration are initializers */
static void parse_extra_dims(struct ast_node *size)
{
	while (current()->type == TOKEN_LBRACKET) {
		position++;
		add_child(size, make_node(NODE_NUMBER, expect(TOKEN_NUMBER)->value));
		expect(TOKEN_RBRACKET);
	}
}

/* nested braces flatten into one row major list so codegen can store the elements in order */
static void parse_init_list(struct ast_node *node, int *count)
{
	expect(TOKEN_LBRACE);
	while (current()->type != TOKEN_RBRACE) {
		if (current()->type == TOKEN_LBRACE) {
			parse_init_list(node, count);
		} else {
			add_child(node, parse_expression());
			(*count)++;
		}
		if (current()->type == TOKEN_COMMA)
			position++;
	}
	expect(TOKEN_RBRACE);
}

/* only the outermost dim can be inferred so the rest divide the count */
static void patch_inferred_size(struct ast_node *size, int init_count)
{
	char buf[16];
	int rest;
	int i;

	rest = 1;
	for (i = 0; i < size->child_count; i++)
		rest *= atoi(size->children[i]->value);
	sprintf(buf, "%d", init_count / rest);
	free(size->value);
	size->value = strdup(buf);
}

/* trailing ; stays unconsumed so the for loop init can reuse this */
static struct ast_node *parse_declaration_inner(void)
{
	struct token *name;
	struct token *size_tok;
	struct ast_node *node;
	struct ast_node *size;
	int is_ptr;
	int is_char;
	int is_unsigned;
	int is_long;
	int infer_size;
	int init_count;

	is_char = 0;
	is_unsigned = 0;
	is_long = 0;

	if (current()->type == TOKEN_UNSIGNED) {
		is_unsigned = 1;
		position++;
	}
	if (current()->type == TOKEN_LONG) {
		is_long = 1;
		position++;
		if (current()->type == TOKEN_INT)
			position++;
	} else if (current()->type == TOKEN_INT) {
		position++;
	} else if (current()->type == TOKEN_CHAR) {
		is_char = 1;
		position++;
	} else if (!is_unsigned) {
		fprintf(stderr, "parser: expected type specifier\n");
		exit(1);
	}

	/* function pointer: type (*name)(param-types) */
	if (current()->type == TOKEN_LPAREN
			&& position + 1 < token_count
			&& tokens[position + 1].type == TOKEN_STAR) {
		position++; /* ( */
		position++; /* * */
		name = expect(TOKEN_IDENTIFIER);
		expect(TOKEN_RPAREN);
		expect(TOKEN_LPAREN);
		while (current()->type != TOKEN_RPAREN)
			position++;
		expect(TOKEN_RPAREN);
		node = make_node(NODE_FPTR_DECLARATION, name->value);
		if (current()->type == TOKEN_ASSIGN) {
			position++;
			add_child(node, parse_expression());
		}
		return node;
	}
	is_ptr = 0;
	if (current()->type == TOKEN_STAR) {
		is_ptr = 1;
		position++;
	}
	name = expect(TOKEN_IDENTIFIER);
	if (current()->type == TOKEN_LBRACKET) {
		position++;
		infer_size = 0;
		if (current()->type == TOKEN_RBRACKET) {
			/* int a[] = {...} -- size comes from the initializer count */
			infer_size = 1;
			size_tok = NULL;
		} else {
			size_tok = expect(TOKEN_NUMBER);
		}
		expect(TOKEN_RBRACKET);
		if (is_ptr)
			node = make_node(is_char ? NODE_CHAR_PTR_ARRAY_DECL
					: NODE_PTR_ARRAY_DECL, name->value);
		else
			node = make_node(is_char ? NODE_CHAR_ARRAY_DECL : NODE_ARRAY_DECL,
					name->value);
		size = make_node(NODE_NUMBER, infer_size ? "0" : size_tok->value);
		add_child(node, size);
		parse_extra_dims(size);
		if (current()->type == TOKEN_ASSIGN) {
			position++;
			init_count = 0;
			parse_init_list(node, &init_count);
			if (infer_size)
				patch_inferred_size(size, init_count);
		}
		return node;
	}
	if (is_ptr)
		node = make_node(is_char ? NODE_CHAR_PTR_DECLARATION : NODE_PTR_DECLARATION,
				name->value);
	else if (is_long)
		node = make_node(NODE_LONG_DECLARATION, name->value);
	else if (is_unsigned && is_char)
		node = make_node(NODE_UNSIGNED_CHAR_DECLARATION, name->value);
	else if (is_unsigned)
		node = make_node(NODE_UNSIGNED_DECLARATION, name->value);
	else
		node = make_node(is_char ? NODE_CHAR_DECLARATION : NODE_DECLARATION,
				name->value);
	if (current()->type == TOKEN_ASSIGN) {
		position++;
		add_child(node, parse_expression());
	}
	return node;
}

static struct ast_node *parse_declaration(void)
{
	struct ast_node *node = parse_declaration_inner();
	expect(TOKEN_SEMICOLON);
	return node;
}

static struct ast_node *parse_if(void)
{
	struct ast_node *node;

	position++;
	node = make_node(NODE_IF, NULL);
	expect(TOKEN_LPAREN);
	add_child(node, parse_expression());
	expect(TOKEN_RPAREN);
	add_child(node, parse_statement());
	if (current()->type == TOKEN_ELSE) {
		position++;
		add_child(node, parse_statement());
	}
	return node;
}

static struct ast_node *parse_while(void)
{
	struct ast_node *node;

	position++;
	node = make_node(NODE_WHILE, NULL);
	expect(TOKEN_LPAREN);
	add_child(node, parse_expression());
	expect(TOKEN_RPAREN);
	add_child(node, parse_statement());
	return node;
}

static struct ast_node *parse_for(void)
{
	struct ast_node *node;

	position++;
	node = make_node(NODE_FOR, NULL);
	expect(TOKEN_LPAREN);
	if (current()->type == TOKEN_INT || current()->type == TOKEN_CHAR)
		add_child(node, parse_declaration_inner());
	else
		add_child(node, parse_expression());
	expect(TOKEN_SEMICOLON);
	add_child(node, parse_expression());
	expect(TOKEN_SEMICOLON);
	add_child(node, parse_expression());
	expect(TOKEN_RPAREN);
	add_child(node, parse_statement());
	return node;
}

static struct ast_node *parse_simple_keyword(enum node_type type)
{
	struct ast_node *node;

	position++;
	node = make_node(type, NULL);
	expect(TOKEN_SEMICOLON);
	return node;
}

static struct ast_node *parse_switch(void)
{
	struct ast_node *node;
	struct ast_node *arm;
	struct token *val;
	struct enum_entry *ent;
	char buf[16];

	position++;
	node = make_node(NODE_SWITCH, NULL);
	expect(TOKEN_LPAREN);
	add_child(node, parse_expression());
	expect(TOKEN_RPAREN);
	expect(TOKEN_LBRACE);
	while (current()->type != TOKEN_RBRACE) {
		if (current()->type == TOKEN_CASE) {
			position++;
			/* case takes a raw number token not parse_expression so enum names need their own lookup */
			if (current()->type == TOKEN_IDENTIFIER) {
				ent = lookup_enum(current()->value);
				if (!ent) {
					fprintf(stderr, "parser: unknown enum constant '%s'\n",
							current()->value);
					exit(1);
				}
				position++;
				sprintf(buf, "%d", ent->value);
				arm = make_node(NODE_CASE, buf);
			} else {
				val = expect(TOKEN_NUMBER);
				arm = make_node(NODE_CASE, val->value);
			}
			expect(TOKEN_COLON);
		} else if (current()->type == TOKEN_DEFAULT) {
			position++;
			expect(TOKEN_COLON);
			arm = make_node(NODE_DEFAULT, NULL);
		} else {
			fprintf(stderr, "parser: expected case or default in switch\n");
			exit(1);
			arm = NULL;
		}
		while (current()->type != TOKEN_CASE
				&& current()->type != TOKEN_DEFAULT
				&& current()->type != TOKEN_RBRACE)
			add_child(arm, parse_statement());
		add_child(node, arm);
	}
	expect(TOKEN_RBRACE);
	return node;
}

static struct ast_node *parse_do_while(void)
{
	struct ast_node *node;

	position++;
	node = make_node(NODE_DO_WHILE, NULL);
	add_child(node, parse_statement());
	expect(TOKEN_WHILE);
	expect(TOKEN_LPAREN);
	add_child(node, parse_expression());
	expect(TOKEN_RPAREN);
	expect(TOKEN_SEMICOLON);
	return node;
}

static struct ast_node *parse_local_struct_decl(void)
{
	struct token *tname;
	struct token *vname;
	struct token *size_tok;
	struct ast_node *node;
	int is_ptr;

	position++;
	tname = expect(TOKEN_IDENTIFIER);
	is_ptr = 0;
	if (current()->type == TOKEN_STAR) {
		is_ptr = 1;
		position++;
	}
	vname = expect(TOKEN_IDENTIFIER);
	if (!is_ptr && current()->type == TOKEN_LBRACKET) {
		position++;
		size_tok = expect(TOKEN_NUMBER);
		expect(TOKEN_RBRACKET);
		expect(TOKEN_SEMICOLON);
		node = make_node(NODE_STRUCT_ARRAY_DECL, tname->value);
		add_child(node, make_node(NODE_VAR, vname->value));
		add_child(node, make_node(NODE_NUMBER, size_tok->value));
		return node;
	}
	node = make_node(is_ptr ? NODE_STRUCT_PTR_DECL : NODE_STRUCT_DECL, tname->value);
	add_child(node, make_node(NODE_VAR, vname->value));
	if (!is_ptr && current()->type == TOKEN_ASSIGN) {
		position++;
		add_child(node, parse_expression());
	}
	expect(TOKEN_SEMICOLON);
	return node;
}

static struct ast_node *parse_struct_def(void)
{
	struct token *name;
	struct token *fname;
	struct token *ftype;
	struct ast_node *node;
	struct ast_node *field;
	int is_char;
	int is_long;
	int is_ptr;

	position++;
	name = expect(TOKEN_IDENTIFIER);
	node = make_node(NODE_STRUCT_DEF, name->value);
	expect(TOKEN_LBRACE);
	while (current()->type != TOKEN_RBRACE) {
		is_char = 0;
		is_long = 0;
		ftype = NULL;
		if (current()->type == TOKEN_STRUCT) {
			position++;
			ftype = expect(TOKEN_IDENTIFIER);
		} else {
			is_char = current()->type == TOKEN_CHAR;
			is_long = current()->type == TOKEN_LONG;
			position++;
			if (is_long && current()->type == TOKEN_INT)
				position++;
		}
		is_ptr = 0;
		if (current()->type == TOKEN_STAR) {
			is_ptr = 1;
			position++;
		}
		fname = expect(TOKEN_IDENTIFIER);
		expect(TOKEN_SEMICOLON);
		if (ftype && !is_ptr) {
			/* a nested struct value would need its own layout copied in */
			fprintf(stderr, "parser: struct field '%s' must be a pointer\n",
					fname->value);
			exit(1);
		}
		if (ftype) {
			field = make_node(NODE_STRUCT_PTR_DECL, ftype->value);
			add_child(field, make_node(NODE_VAR, fname->value));
		} else if (is_ptr) {
			field = make_node(is_char ? NODE_CHAR_PTR_DECLARATION
					: NODE_PTR_DECLARATION, fname->value);
		} else if (is_long) {
			field = make_node(NODE_LONG_DECLARATION, fname->value);
		} else {
			field = make_node(is_char ? NODE_CHAR_DECLARATION
					: NODE_DECLARATION, fname->value);
		}
		add_child(node, field);
	}
	expect(TOKEN_RBRACE);
	expect(TOKEN_SEMICOLON);
	return node;
}

static struct ast_node *parse_global_struct(void)
{
	struct token *tname;
	struct token *vname;
	struct token *size_tok;
	struct ast_node *node;
	int is_ptr;
	int init_count;

	position++;
	tname = expect(TOKEN_IDENTIFIER);
	is_ptr = 0;
	if (current()->type == TOKEN_STAR) {
		is_ptr = 1;
		position++;
	}
	vname = expect(TOKEN_IDENTIFIER);
	/* value holds the type so the var name lives in the child like the local form */
	if (!is_ptr && current()->type == TOKEN_LBRACKET) {
		position++;
		size_tok = expect(TOKEN_NUMBER);
		expect(TOKEN_RBRACKET);
		node = make_node(NODE_GLOBAL_STRUCT_ARRAY, tname->value);
		add_child(node, make_node(NODE_VAR, vname->value));
		add_child(node, make_node(NODE_NUMBER, size_tok->value));
		if (current()->type == TOKEN_ASSIGN) {
			position++;
			init_count = 0;
			parse_init_list(node, &init_count);
		}
		expect(TOKEN_SEMICOLON);
		return node;
	}
	node = make_node(is_ptr ? NODE_GLOBAL_STRUCT_PTR : NODE_GLOBAL_STRUCT,
			tname->value);
	add_child(node, make_node(NODE_VAR, vname->value));
	if (current()->type == TOKEN_ASSIGN) {
		position++;
		init_count = 0;
		parse_init_list(node, &init_count);
	}
	expect(TOKEN_SEMICOLON);
	return node;
}

/* nothing goes in the tree the constants just get remembered */
static void parse_enum_def(void)
{
	struct token *name;
	int value;

	position++;
	if (current()->type == TOKEN_IDENTIFIER)
		position++; /* tag name allowed but unused */
	expect(TOKEN_LBRACE);
	value = 0;
	while (current()->type != TOKEN_RBRACE) {
		name = expect(TOKEN_IDENTIFIER);
		if (current()->type == TOKEN_ASSIGN) {
			position++;
			value = atoi(expect(TOKEN_NUMBER)->value);
		}
		if (enum_count >= MAX_ENUMS) {
			fprintf(stderr, "parser: too many enum constants\n");
			exit(1);
		}
		enum_map[enum_count].name = strdup(name->value);
		enum_map[enum_count].value = value;
		enum_count++;
		value++;
		if (current()->type == TOKEN_COMMA)
			position++;
	}
	expect(TOKEN_RBRACE);
	expect(TOKEN_SEMICOLON);
}

static struct ast_node *parse_statement(void)
{
	struct ast_node *node;

	switch (current()->type) {
	case TOKEN_LBRACE:	return parse_block();
	case TOKEN_RETURN:	return parse_return();
	case TOKEN_INT:
	case TOKEN_CHAR:
	case TOKEN_UNSIGNED:
	case TOKEN_LONG:	return parse_declaration();
	case TOKEN_STRUCT:	return parse_local_struct_decl();
	case TOKEN_IF:		return parse_if();
	case TOKEN_WHILE:	return parse_while();
	case TOKEN_FOR:		return parse_for();
	case TOKEN_BREAK:	return parse_simple_keyword(NODE_BREAK);
	case TOKEN_CONTINUE:	return parse_simple_keyword(NODE_CONTINUE);
	case TOKEN_DO:		return parse_do_while();
	case TOKEN_SWITCH:	return parse_switch();
	default:
		node = parse_expression();
		expect(TOKEN_SEMICOLON);
		return node;
	}
}

static struct ast_node *parse_function(void)
{
	struct token *name;
	struct token *pname;
	struct ast_node *node;
	struct token *sret_type;
	int is_ptr_param;
	int is_char_param;

	sret_type = NULL;
	if (current()->type == TOKEN_STRUCT) {
		position++;
		sret_type = expect(TOKEN_IDENTIFIER);
	} else {
		/* return type gets consumed and ignored since every value is int sized anyway */
		if (current()->type == TOKEN_UNSIGNED)
			position++;
		if (current()->type == TOKEN_INT || current()->type == TOKEN_VOID
				|| current()->type == TOKEN_CHAR || current()->type == TOKEN_LONG)
			position++;
		else
			expect(TOKEN_INT);
	}
	name = expect(TOKEN_IDENTIFIER);
	node = make_node(NODE_FUNCTION, name->value);
	if (sret_type)
		add_child(node, make_node(NODE_STRUCT_RET, sret_type->value));
	expect(TOKEN_LPAREN);
	while (current()->type != TOKEN_RPAREN) {
		/* function pointer param: type (*name)(param-types) */
		if ((current()->type == TOKEN_INT || current()->type == TOKEN_CHAR
				|| current()->type == TOKEN_VOID
				|| current()->type == TOKEN_LONG
				|| current()->type == TOKEN_UNSIGNED)
				&& position + 2 < token_count
				&& tokens[position + 1].type == TOKEN_LPAREN
				&& tokens[position + 2].type == TOKEN_STAR) {
			position++; /* type */
			position++; /* ( */
			position++; /* * */
			pname = expect(TOKEN_IDENTIFIER);
			expect(TOKEN_RPAREN);
			expect(TOKEN_LPAREN);
			while (current()->type != TOKEN_RPAREN)
				position++;
			expect(TOKEN_RPAREN);
			add_child(node, make_node(NODE_FPTR_DECLARATION, pname->value));
			if (current()->type == TOKEN_COMMA)
				position++;
			continue;
		}
		if (current()->type == TOKEN_STRUCT) {
			struct token *stype;
			struct ast_node *param;
			position++;
			stype = expect(TOKEN_IDENTIFIER);
			if (current()->type == TOKEN_STAR) {
				position++;
				pname = expect(TOKEN_IDENTIFIER);
				param = make_node(NODE_STRUCT_PTR_DECL, stype->value);
			} else {
				pname = expect(TOKEN_IDENTIFIER);
				param = make_node(NODE_STRUCT_VAL_PARAM, stype->value);
			}
			add_child(param, make_node(NODE_VAR, pname->value));
			add_child(node, param);
		} else {
			int is_unsigned_param = 0;
			int is_long_param = 0;
			is_char_param = 0;
			if (current()->type == TOKEN_UNSIGNED) {
				is_unsigned_param = 1;
				position++;
			}
			if (current()->type == TOKEN_LONG) {
				is_long_param = 1;
				position++;
				if (current()->type == TOKEN_INT)
					position++;
			} else if (current()->type == TOKEN_INT) {
				position++;
			} else if (current()->type == TOKEN_CHAR) {
				is_char_param = 1;
				position++;
			}
			is_ptr_param = 0;
			if (current()->type == TOKEN_STAR) {
				is_ptr_param = 1;
				position++;
			}
			pname = expect(TOKEN_IDENTIFIER);
			if (is_ptr_param)
				add_child(node, make_node(
						is_char_param ? NODE_CHAR_PTR_DECLARATION : NODE_PTR_DECLARATION,
						pname->value));
			else if (is_long_param)
				add_child(node, make_node(NODE_LONG_DECLARATION, pname->value));
			else if (is_unsigned_param && is_char_param)
				add_child(node, make_node(NODE_UNSIGNED_CHAR_DECLARATION, pname->value));
			else if (is_unsigned_param)
				add_child(node, make_node(NODE_UNSIGNED_DECLARATION, pname->value));
			else
				add_child(node, make_node(
						is_char_param ? NODE_CHAR_DECLARATION : NODE_DECLARATION,
						pname->value));
		}
		if (current()->type == TOKEN_COMMA)
			position++;
	}
	expect(TOKEN_RPAREN);
	if (current()->type == TOKEN_SEMICOLON) {
		position++;
		node->type = NODE_PROTO;
		return node;
	}
	add_child(node, parse_block()); /* body is always last child */
	return node;
}

static struct ast_node *parse_global(void)
{
	struct token *name;
	struct token *size_tok;
	struct ast_node *node;
	struct ast_node *size;
	int is_char;
	int is_ptr;
	int infer_size;
	int init_count;

	is_char = (current()->type == TOKEN_CHAR);
	position++;
	is_ptr = 0;
	if (current()->type == TOKEN_STAR) {
		is_ptr = 1;
		position++;
	}
	name = expect(TOKEN_IDENTIFIER);
	if (current()->type == TOKEN_LBRACKET) {
		position++;
		infer_size = 0;
		if (current()->type == TOKEN_RBRACKET) {
			infer_size = 1;
			size_tok = NULL;
		} else {
			size_tok = expect(TOKEN_NUMBER);
		}
		expect(TOKEN_RBRACKET);
		if (is_ptr)
			node = make_node(is_char ? NODE_GLOBAL_CHAR_PTR_ARRAY
					: NODE_GLOBAL_PTR_ARRAY, name->value);
		else
			node = make_node(is_char ? NODE_GLOBAL_CHAR_ARRAY
					: NODE_GLOBAL_ARRAY, name->value);
		size = make_node(NODE_NUMBER, infer_size ? "0" : size_tok->value);
		add_child(node, size);
		parse_extra_dims(size);
		if (current()->type == TOKEN_ASSIGN) {
			position++;
			init_count = 0;
			parse_init_list(node, &init_count);
			if (infer_size)
				patch_inferred_size(size, init_count);
		} else if (infer_size) {
			fprintf(stderr, "parser: global array '%s' needs a size or an initializer\n",
					name->value);
			exit(1);
		}
		expect(TOKEN_SEMICOLON);
		return node;
	}
	if (is_ptr)
		node = make_node(is_char ? NODE_GLOBAL_CHAR_PTR : NODE_GLOBAL_PTR,
				name->value);
	else
		node = make_node(NODE_GLOBAL, name->value);
	/* anything non constant gets rejected in codegen where the fold happens */
	if (current()->type == TOKEN_ASSIGN) {
		position++;
		add_child(node, parse_expression());
	}
	expect(TOKEN_SEMICOLON);
	return node;
}

struct ast_node *parse(struct token *toks, int count)
{
	struct ast_node *program;

	tokens = toks;
	token_count = count;
	position = 0;
	enum_count = 0;

	program = make_node(NODE_PROGRAM, NULL);
	while (current()->type != TOKEN_EOF) {
		if (current()->type == TOKEN_STRUCT) {
			if (position + 2 < token_count
					&& tokens[position + 2].type == TOKEN_LBRACE)
				add_child(program, parse_struct_def());
			/* struct T name(...) is a struct-returning function */
			else if (position + 3 < token_count
					&& tokens[position + 2].type == TOKEN_IDENTIFIER
					&& tokens[position + 3].type == TOKEN_LPAREN)
				add_child(program, parse_function());
			else
				add_child(program, parse_global_struct());
		} else if (current()->type == TOKEN_ENUM) {
			parse_enum_def();
		/* peek ahead -- int/void/char NAME ( means function otherwise global */
		} else if (position + 2 < token_count
				&& tokens[position + 2].type == TOKEN_LPAREN) {
			add_child(program, parse_function());
		} else {
			add_child(program, parse_global());
		}
	}
	return program;
}

void free_ast(struct ast_node *node)
{
	int i;

	if (!node)
		return;
	for (i = 0; i < node->child_count; i++)
		free_ast(node->children[i]);
	free(node->children);
	free(node->value);
	free(node);
}
