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
		fprintf(stderr, "parser: unexpected token '%s'\n", current()->value);
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

/* for now expressions are just numbers */
static struct ast_node *parse_expression(void)
{
	struct token *tok = expect(TOKEN_NUMBER);
	return make_node(NODE_NUMBER, tok->value);
}

/* parse a return statement */
static struct ast_node *parse_statement(void)
{
	expect(TOKEN_RETURN);
	struct ast_node *node =  make_node(NODE_RETURN, NULL);
	add_child(node, parse_expression());
	expect(TOKEN_SEMICOLON);
	return node;
}

/* parse a function */
static struct ast_node *parse_function(void)
{
	expect(TOKEN_INT);
	struct token *name = expect(TOKEN_IDENTIFIER);
	struct ast_node *node = make_node(NODE_FUNCTION, name->value);
	expect(TOKEN_LPAREN);
	expect(TOKEN_RPAREN);
	expect(TOKEN_LBRACE);
	add_child(node, parse_statement());
	expect(TOKEN_RBRACE);
	return node;
}

/* entry point */
struct ast_node *parse(struct token *toks, int count)
{
	tokens = toks;
	token_count = count;
	position = 0;

	struct ast_node *program = make_node(NODE_PROGRAM, NULL);
	add_child(program, parse_function());
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
