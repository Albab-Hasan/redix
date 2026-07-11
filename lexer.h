#ifndef LEXER_H
#define LEXER_H

enum token_type {
	TOKEN_INT,
	TOKEN_VOID,
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
	TOKEN_ASSIGN,
	TOKEN_IF,
	TOKEN_ELSE,
	TOKEN_WHILE,
	TOKEN_FOR,
	TOKEN_BREAK,
	TOKEN_CONTINUE,
	TOKEN_COMMA,
	TOKEN_INC,
	TOKEN_DEC,
	TOKEN_PLUS_ASSIGN,
	TOKEN_MINUS_ASSIGN,
	TOKEN_STAR_ASSIGN,
	TOKEN_SLASH_ASSIGN,
	TOKEN_AMPERSAND,
	TOKEN_QUESTION,
	TOKEN_COLON,
	TOKEN_LBRACKET,
	TOKEN_RBRACKET,
	TOKEN_CHAR,
	TOKEN_STRING_LITERAL,
	TOKEN_SIZEOF,
	TOKEN_STRUCT,
	TOKEN_DOT,
	TOKEN_ARROW,
	TOKEN_PIPE,
	TOKEN_CARET,
	TOKEN_LSHIFT,
	TOKEN_RSHIFT,
	TOKEN_PERCENT,
	TOKEN_SWITCH,
	TOKEN_CASE,
	TOKEN_DEFAULT,
	TOKEN_DO,
	TOKEN_ENUM,
};

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
