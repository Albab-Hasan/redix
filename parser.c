#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "lexer.h"

static int position;
static struct token *tokens;
static int token_count;

/* return the token at the current position */
static struct token *current(void)
{
	return &tokens[position];
}

/* check that the current is a expected one and move past it */
static struct token *expect(enum token_type type)
{
	if (position >= token_count || current()->type != type) {
		fprintf(stderr, "parser: unexpected token '%s'\n",
				current()->value);
		exit(1);
	}
	return &tokens[position++];
}

/* allocate a new ast node */
static struct ast_node *make_node(enum node_type type, char *value)
{
	struct ast_node *node = malloc(sizeof(struct ast_node));
	node->type = type;
	node->value = value ? strdup(value) : NULL;
	node->child_count = 0;
	node->children = NULL;
	return node;
}

/* attach a child node to a parent */
static void add_child(struct ast_node *parent, struct ast_node *child)
{
	parent->child_count++;
	parent->children = realloc(parent->children,
			parent->child_count * sizeof(struct ast_node *));
	parent->children[parent->child_count - 1] = child;
}

static struct ast_node *parse_statement(void);
static struct ast_node *parse_expression(void);

/* number variable function call or parenthesized expression */
static struct ast_node *parse_primary(void)
{
	struct token *tok;
	struct ast_node *node;

	if (current()->type == TOKEN_NUMBER) {
		tok = &tokens[position++];
		return make_node(NODE_NUMBER, tok->value);
	}

	if (current()->type == TOKEN_IDENTIFIER) {
		tok = &tokens[position++];
		if (current()->type == TOKEN_LPAREN) {
			position++; /* consume ( */
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
		return make_node(NODE_VAR, tok->value);
	}

	if (current()->type == TOKEN_LPAREN) {
		position++; /* consume ( */
		node = parse_expression();
		expect(TOKEN_RPAREN);
		return node;
	}

	fprintf(stderr, "parser: expected number or variable got '%s'\n",
			current()->value);
	exit(1);
	return NULL;
}

/* unary operators - ~ ! */
static struct ast_node *parse_unary(void)
{
	struct token *tok;
	struct ast_node *node;

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
		/* operand must be a plain variable */
		tok = expect(TOKEN_IDENTIFIER);
		return make_node(op == TOKEN_INC ? NODE_PREFIX_INC : NODE_PREFIX_DEC,
				tok->value);
	}
	return parse_primary();
}

/* generic left-associative binary parser
 * matchfn tells us which token types are operators at this level
 * next is the parser for the tighter precedence level below us */
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
	return t == TOKEN_STAR || t == TOKEN_SLASH;
}

static int is_add(enum token_type t)
{
	return t == TOKEN_PLUS || t == TOKEN_MINUS;
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

static struct ast_node *parse_relational(void)
{
	return parse_binop(parse_additive, is_rel);
}

static struct ast_node *parse_equality(void)
{
	return parse_binop(parse_relational, is_eq);
}

static struct ast_node *parse_logical_and(void)
{
	return parse_binop(parse_equality, is_and);
}

static struct ast_node *parse_logical_or(void)
{
	return parse_binop(parse_logical_and, is_or);
}

/* assignment or regular expression */
static struct ast_node *parse_expression(void)
{
	struct ast_node *left;
	struct ast_node *node;
	struct ast_node *binary;
	const char *op;

	left = parse_logical_or();

	if (current()->type == TOKEN_ASSIGN) {
		position++;
		node = make_node(NODE_ASSIGN, left->value);
		free_ast(left);
		add_child(node, parse_expression()); /* right associative */
		return node;
	}

	/* desugar: x op= e  ->  x = x op e */
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
		add_child(binary, parse_expression()); /* right associative */
		add_child(node, binary);
		return node;
	}

	return left;
}

/* parse a block { stmt* } into a compound node */
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

/* parse int name [= expr] without consuming the trailing ; */
static struct ast_node *parse_declaration_inner(void)
{
	struct token *name;
	struct ast_node *node;

	position++; /* consume int */
	name = expect(TOKEN_IDENTIFIER);
	node = make_node(NODE_DECLARATION, name->value);
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
	add_child(node, parse_expression()); /* condition */
	expect(TOKEN_RPAREN);
	add_child(node, parse_statement());  /* then */
	if (current()->type == TOKEN_ELSE) {
		position++;
		add_child(node, parse_statement()); /* else */
	}
	return node;
}

static struct ast_node *parse_while(void)
{
	struct ast_node *node;

	position++;
	node = make_node(NODE_WHILE, NULL);
	expect(TOKEN_LPAREN);
	add_child(node, parse_expression()); /* condition */
	expect(TOKEN_RPAREN);
	add_child(node, parse_statement());  /* body */
	return node;
}

static struct ast_node *parse_for(void)
{
	struct ast_node *node;

	position++;
	node = make_node(NODE_FOR, NULL);
	expect(TOKEN_LPAREN);
	if (current()->type == TOKEN_INT)
		add_child(node, parse_declaration_inner());
	else
		add_child(node, parse_expression());
	expect(TOKEN_SEMICOLON);
	add_child(node, parse_expression()); /* condition */
	expect(TOKEN_SEMICOLON);
	add_child(node, parse_expression()); /* increment */
	expect(TOKEN_RPAREN);
	add_child(node, parse_statement());  /* body */
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

/* parse a single statement */
static struct ast_node *parse_statement(void)
{
	struct ast_node *node;

	switch (current()->type) {
	case TOKEN_LBRACE:	return parse_block();
	case TOKEN_RETURN:	return parse_return();
	case TOKEN_INT:		return parse_declaration();
	case TOKEN_IF:		return parse_if();
	case TOKEN_WHILE:	return parse_while();
	case TOKEN_FOR:		return parse_for();
	case TOKEN_BREAK:	return parse_simple_keyword(NODE_BREAK);
	case TOKEN_CONTINUE:	return parse_simple_keyword(NODE_CONTINUE);
	default:
		/* expression statement like assignments */
		node = parse_expression();
		expect(TOKEN_SEMICOLON);
		return node;
	}
}

/* parse a function -- params stored as NODE_DECLARATION children before body */
static struct ast_node *parse_function(void)
{
	struct token *name;
	struct token *pname;
	struct ast_node *node;

	/* return type int or void for now we just consume it */
	if (current()->type == TOKEN_INT || current()->type == TOKEN_VOID)
		position++;
	else
		expect(TOKEN_INT);
	name = expect(TOKEN_IDENTIFIER);
	node = make_node(NODE_FUNCTION, name->value);
	expect(TOKEN_LPAREN);
	while (current()->type != TOKEN_RPAREN) {
		expect(TOKEN_INT);
		pname = expect(TOKEN_IDENTIFIER);
		add_child(node, make_node(NODE_DECLARATION, pname->value));
		if (current()->type == TOKEN_COMMA)
			position++;
	}
	expect(TOKEN_RPAREN);
	add_child(node, parse_block()); /* body is always last child */
	return node;
}

/* global variable declaration: int name [= number]; */
static struct ast_node *parse_global(void)
{
	struct token *name;
	struct ast_node *node;

	position++; /* consume int */
	name = expect(TOKEN_IDENTIFIER);
	node = make_node(NODE_GLOBAL, name->value);
	if (current()->type == TOKEN_ASSIGN) {
		position++;
		if (current()->type != TOKEN_NUMBER) {
			fprintf(stderr, "parser: global initializer must be a constant\n");
			exit(1);
		}
		add_child(node, make_node(NODE_NUMBER, tokens[position++].value));
	}
	expect(TOKEN_SEMICOLON);
	return node;
}

/* entry point */
struct ast_node *parse(struct token *toks, int count)
{
	struct ast_node *program;

	tokens = toks;
	token_count = count;
	position = 0;

	program = make_node(NODE_PROGRAM, NULL);
	while (current()->type != TOKEN_EOF) {
		/* peek: int/void NAME ( -> function, otherwise -> global */
		if (position + 2 < token_count
				&& tokens[position + 2].type == TOKEN_LPAREN)
			add_child(program, parse_function());
		else
			add_child(program, parse_global());
	}
	return program;
}

/* free the entire tree */
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
