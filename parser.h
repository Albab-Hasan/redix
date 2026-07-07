#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

/* types of nodes in the abstrct syntax tree */
enum node_type {
	NODE_PROGRAM,
	NODE_FUNCTION,
	NODE_GLOBAL,
	NODE_RETURN,
	NODE_NUMBER,
	NODE_UNARY,
	NODE_BINARY,
	NODE_DECLARATION,
	NODE_ASSIGN,
	NODE_VAR,
	NODE_COMPOUND,
	NODE_IF,
	NODE_WHILE,
	NODE_FOR,
	NODE_BREAK,
	NODE_CONTINUE,
	NODE_CALL,
	NODE_PREFIX_INC,
	NODE_PREFIX_DEC,
	NODE_POSTFIX_INC,
	NODE_POSTFIX_DEC,
	NODE_ADDR_OF,
	NODE_DEREF,
	NODE_DEREF_ASSIGN,
	NODE_PTR_DECLARATION,
	NODE_TERNARY,
	NODE_ARRAY_DECL,
	NODE_CHAR_DECLARATION,
	NODE_CHAR_PTR_DECLARATION,
	NODE_CHAR_ARRAY_DECL,
	NODE_STRING,
	NODE_STRUCT_DEF,
	NODE_STRUCT_DECL,
	NODE_STRUCT_PTR_DECL,
	NODE_MEMBER,
	NODE_MEMBER_ASSIGN,
	NODE_PTR_MEMBER,
	NODE_PTR_MEMBER_ASSIGN,
	NODE_SWITCH,
	NODE_CASE,
	NODE_DEFAULT,
	NODE_DO_WHILE,
	NODE_GLOBAL_PTR,
	NODE_GLOBAL_CHAR_PTR,
	NODE_GLOBAL_ARRAY,
	NODE_GLOBAL_CHAR_ARRAY,
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
