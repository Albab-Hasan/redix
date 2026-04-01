#ifndef LEXER_H
#define LEXER_H

/* every possible token type */
enum token_type {
	TOKEN_INT,
	TOKEN_RETURN,
	TOKEN_IDENTIFIER,
	TOKEN_NUMBER,
	TOKEN_LPAREN,
	TOKEN_RPAREN,
	TOKEN_LBRACE,
	TOKEN_RBRACE,
	TOKEN_SEMICOLON,
	TOKEN_MINUS,
	TOKEN_TILDE,
	TOKEN_BANG,
	TOKEN_EOF,
	TOKEN_PLUS,
	TOKEN_STAR,
	TOKEN_SLASH,
	TOKEN_LT,
	TOKEN_GT,
	TOKEN_LTE,
	TOKEN_GTE,
	TOKEN_EQ,
	TOKEN_NEQ,
	TOKEN_AND,
	TOKEN_OR,
};

/* a tokens type and the actual text it came from */
struct token {
	enum token_type type;
	char *value;
};

/* takes source code string, returns array of tokens 
 * count is set to how many tokens were made */
struct token *lexer_tokenize(const char *source, int *count);

/* for debugging this returns the name of a token type as a string */
const char *token_type_name(enum token_type type);

#endif
