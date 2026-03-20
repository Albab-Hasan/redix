#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

/* types of nodes in the abstrct syntax tree */
enum node_type {
	NODE_PROGRAM,
	NODE_FUNCTION,
	NODE_RETURN,
	NODE_NUMBER
};

/* a node in the abstrct syntax tree that can have child nodes */
struct ast_node {
	enum node_type type;
	char *value;
	struct ast_node **children;
	int child_count;
};

struct ast_node *parse(struct token *tokens, int token_count);
void free_ast(struct ast_node *node);

# endif
