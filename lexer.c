#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

/* create a token and return it */
static struct token make_token(enum token_type type, const char *value)
{
	struct token t;
	t.type = type;
	t.value = strdup(value);
	return t;
}

const char *token_type_name(enum token_type type)
{
	switch (type) {
	case TOKEN_INT:		return "TOKEN_INT";
	case TOKEN_VOID:	return "TOKEN_VOID";
	case TOKEN_RETURN:	return "TOKEN_RETURN";
	case TOKEN_IDENTIFIER:	return "TOKEN_IDENTIFIER";
	case TOKEN_NUMBER:	return "TOKEN_NUMBER";
	case TOKEN_LPAREN:	return "TOKEN_LPAREN";
	case TOKEN_RPAREN:	return "TOKEN_RPAREN";
	case TOKEN_LBRACE:	return "TOKEN_LBRACE";
	case TOKEN_RBRACE:	return "TOKEN_RBRACE";
	case TOKEN_SEMICOLON:	return "TOKEN_SEMICOLON";
	case TOKEN_MINUS:	return "TOKEN_MINUS";
	case TOKEN_TILDE:	return "TOKEN_TILDE";
	case TOKEN_BANG:	return "TOKEN_BANG";
	case TOKEN_EOF:		return "TOKEN_EOF";
	case TOKEN_PLUS:	return "TOKEN_PLUS";
	case TOKEN_STAR:	return "TOKEN_STAR";
	case TOKEN_SLASH:	return "TOKEN_SLASH";
	case TOKEN_LT:		return "TOKEN_LT";
	case TOKEN_GT:		return "TOKEN_GT";
	case TOKEN_LTE:		return "TOKEN_LTE";
	case TOKEN_GTE:		return "TOKEN_GTE";
	case TOKEN_EQ:		return "TOKEN_EQ";
	case TOKEN_NEQ:		return "TOKEN_NEQ";
	case TOKEN_AND:		return "TOKEN_AND";
	case TOKEN_OR:		return "TOKEN_OR";
	case TOKEN_ASSIGN:	return "TOKEN_ASSIGN";
	case TOKEN_IF:		return "TOKEN_IF";
	case TOKEN_ELSE:	return "TOKEN_ELSE";
	case TOKEN_WHILE:	return "TOKEN_WHILE";
	case TOKEN_FOR:		return "TOKEN_FOR";
	case TOKEN_BREAK:	return "TOKEN_BREAK";
	case TOKEN_CONTINUE:	return "TOKEN_CONTINUE";
	case TOKEN_COMMA:	return "TOKEN_COMMA";
	case TOKEN_INC:			return "TOKEN_INC";
	case TOKEN_DEC:			return "TOKEN_DEC";
	case TOKEN_PLUS_ASSIGN:		return "TOKEN_PLUS_ASSIGN";
	case TOKEN_MINUS_ASSIGN:	return "TOKEN_MINUS_ASSIGN";
	case TOKEN_STAR_ASSIGN:		return "TOKEN_STAR_ASSIGN";
	case TOKEN_SLASH_ASSIGN:	return "TOKEN_SLASH_ASSIGN";
	case TOKEN_AMPERSAND:		return "TOKEN_AMPERSAND";
	}
	return "UNKNOWN";
}

/* keyword lookup table -- order does not matter */
static const struct {
	const char *word;
	enum token_type type;
} keywords[] = {
	{ "int",      TOKEN_INT },
	{ "void",     TOKEN_VOID },
	{ "return",   TOKEN_RETURN },
	{ "if",       TOKEN_IF },
	{ "else",     TOKEN_ELSE },
	{ "while",    TOKEN_WHILE },
	{ "for",      TOKEN_FOR },
	{ "break",    TOKEN_BREAK },
	{ "continue", TOKEN_CONTINUE },
};

#define NKEYWORDS (sizeof(keywords) / sizeof(keywords[0]))

/* match an identifier-shaped word against the keyword table
 * returns TOKEN_IDENTIFIER if no keyword matches */
static enum token_type lookup_keyword(const char *word)
{
	size_t i;

	for (i = 0; i < NKEYWORDS; i++) {
		if (strcmp(word, keywords[i].word) == 0)
			return keywords[i].type;
	}
	return TOKEN_IDENTIFIER;
}

/* scan a number literal starting at *pos and append a TOKEN_NUMBER */
static void scan_number(const char *source, int *pos,
		struct token *tokens, int *ntokens)
{
	int start = *pos;
	int length;
	char *number;

	while (isdigit(source[*pos]))
		(*pos)++;
	length = *pos - start;
	number = malloc(length + 1);
	memcpy(number, &source[start], length);
	number[length] = '\0';
	tokens[(*ntokens)++] = (struct token){ TOKEN_NUMBER, number };
}

/* scan an identifier or keyword starting at *pos */
static void scan_identifier(const char *source, int *pos,
		struct token *tokens, int *ntokens)
{
	int start = *pos;
	int length;
	char *word;
	enum token_type type;

	while (isalpha(source[*pos]) || isdigit(source[*pos])
			|| source[*pos] == '_')
		(*pos)++;
	length = *pos - start;
	word = malloc(length + 1);
	memcpy(word, &source[start], length);
	word[length] = '\0';

	type = lookup_keyword(word);
	tokens[(*ntokens)++] = (struct token){ type, word };
}

struct token *lexer_tokenize(const char *source, int *count)
{
	int capacity = 64;
	int ntokens = 0;
	int position = 0;
	struct token *tokens = malloc(sizeof(struct token) * capacity);
	char c;

	while (source[position] != '\0') {

		/* skip whitespace */
		if (isspace(source[position])) {
			position++;
			continue;
		}

		/* skip line comments */
		if (source[position] == '/' && source[position + 1] == '/') {
			while (source[position] != '\0' && source[position] != '\n')
				position++;
			continue;
		}

		/* grow the array if needed */
		if (ntokens + 1 >= capacity) {
			capacity *= 2;
			tokens = realloc(tokens, sizeof(struct token) * capacity);
		}

		c = source[position];

		switch (c) {
		case '(':
			tokens[ntokens++] = make_token(TOKEN_LPAREN, "(");
			position++;
			break;
		case ')':
			tokens[ntokens++] = make_token(TOKEN_RPAREN, ")");
			position++;
			break;
		case '{':
			tokens[ntokens++] = make_token(TOKEN_LBRACE, "{");
			position++;
			break;
		case '}':
			tokens[ntokens++] = make_token(TOKEN_RBRACE, "}");
			position++;
			break;
		case ';':
			tokens[ntokens++] = make_token(TOKEN_SEMICOLON, ";");
			position++;
			break;
		case ',':
			tokens[ntokens++] = make_token(TOKEN_COMMA, ",");
			position++;
			break;
		case '+':
			if (source[position + 1] == '+') {
				tokens[ntokens++] = make_token(TOKEN_INC, "++");
				position += 2;
			} else if (source[position + 1] == '=') {
				tokens[ntokens++] = make_token(TOKEN_PLUS_ASSIGN, "+=");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_PLUS, "+");
				position++;
			}
			break;
		case '-':
			if (source[position + 1] == '-') {
				tokens[ntokens++] = make_token(TOKEN_DEC, "--");
				position += 2;
			} else if (source[position + 1] == '=') {
				tokens[ntokens++] = make_token(TOKEN_MINUS_ASSIGN, "-=");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_MINUS, "-");
				position++;
			}
			break;
		case '*':
			if (source[position + 1] == '=') {
				tokens[ntokens++] = make_token(TOKEN_STAR_ASSIGN, "*=");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_STAR, "*");
				position++;
			}
			break;
		case '/':
			if (source[position + 1] == '=') {
				tokens[ntokens++] = make_token(TOKEN_SLASH_ASSIGN, "/=");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_SLASH, "/");
				position++;
			}
			break;
		case '~':
			tokens[ntokens++] = make_token(TOKEN_TILDE, "~");
			position++;
			break;
		case '!':
			if (source[position + 1] == '=') {
				tokens[ntokens++] = make_token(TOKEN_NEQ, "!=");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_BANG, "!");
				position++;
			}
			break;
		case '<':
			if (source[position + 1] == '=') {
				tokens[ntokens++] = make_token(TOKEN_LTE, "<=");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_LT, "<");
				position++;
			}
			break;
		case '>':
			if (source[position + 1] == '=') {
				tokens[ntokens++] = make_token(TOKEN_GTE, ">=");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_GT, ">");
				position++;
			}
			break;
		case '=':
			if (source[position + 1] == '=') {
				tokens[ntokens++] = make_token(TOKEN_EQ, "==");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_ASSIGN, "=");
				position++;
			}
			break;
		case '&':
			if (source[position + 1] == '&') {
				tokens[ntokens++] = make_token(TOKEN_AND, "&&");
				position += 2;
			} else {
				tokens[ntokens++] = make_token(TOKEN_AMPERSAND, "&");
				position++;
			}
			break;
		case '|':
			if (source[position + 1] == '|') {
				tokens[ntokens++] = make_token(TOKEN_OR, "||");
				position += 2;
			} else {
				fprintf(stderr, "redix: unexpected character '|'\n");
				exit(1);
			}
			break;
		default:
			if (isdigit(c)) {
				scan_number(source, &position, tokens, &ntokens);
			} else if (isalpha(c) || c == '_') {
				scan_identifier(source, &position, tokens, &ntokens);
			} else {
				fprintf(stderr, "redix: unexpected character '%c'\n", c);
				exit(1);
			}
			break;
		}
	}

	tokens[ntokens++] = make_token(TOKEN_EOF, "EOF");
	*count = ntokens;
	return tokens;
}
