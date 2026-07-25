#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

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
	case TOKEN_QUESTION:		return "TOKEN_QUESTION";
	case TOKEN_COLON:		return "TOKEN_COLON";
	case TOKEN_LBRACKET:		return "TOKEN_LBRACKET";
	case TOKEN_RBRACKET:		return "TOKEN_RBRACKET";
	case TOKEN_CHAR:		return "TOKEN_CHAR";
	case TOKEN_STRING_LITERAL:	return "TOKEN_STRING_LITERAL";
	case TOKEN_SIZEOF:		return "TOKEN_SIZEOF";
	case TOKEN_STRUCT:		return "TOKEN_STRUCT";
	case TOKEN_DOT:			return "TOKEN_DOT";
	case TOKEN_ARROW:		return "TOKEN_ARROW";
	case TOKEN_PIPE:		return "TOKEN_PIPE";
	case TOKEN_CARET:		return "TOKEN_CARET";
	case TOKEN_LSHIFT:		return "TOKEN_LSHIFT";
	case TOKEN_RSHIFT:		return "TOKEN_RSHIFT";
	case TOKEN_PERCENT:		return "TOKEN_PERCENT";
	case TOKEN_SWITCH:		return "TOKEN_SWITCH";
	case TOKEN_CASE:		return "TOKEN_CASE";
	case TOKEN_DEFAULT:		return "TOKEN_DEFAULT";
	case TOKEN_DO:			return "TOKEN_DO";
	case TOKEN_ENUM:		return "TOKEN_ENUM";
	case TOKEN_UNSIGNED:		return "TOKEN_UNSIGNED";
	case TOKEN_LONG:		return "TOKEN_LONG";
	}
	return "UNKNOWN";
}

/* order does not matter just a linear scan */
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
	{ "char",     TOKEN_CHAR },
	{ "sizeof",   TOKEN_SIZEOF },
	{ "struct",   TOKEN_STRUCT },
	{ "switch",   TOKEN_SWITCH },
	{ "case",     TOKEN_CASE },
	{ "default",  TOKEN_DEFAULT },
	{ "do",       TOKEN_DO },
	{ "enum",     TOKEN_ENUM },
	{ "unsigned", TOKEN_UNSIGNED },
	{ "long",     TOKEN_LONG },
};

#define NKEYWORDS (sizeof(keywords) / sizeof(keywords[0]))

static enum token_type lookup_keyword(const char *word)
{
	size_t i;

	for (i = 0; i < NKEYWORDS; i++) {
		if (strcmp(word, keywords[i].word) == 0)
			return keywords[i].type;
	}
	return TOKEN_IDENTIFIER;
}

/* expansion happens at lex time so the parser never sees macros */
struct macro_entry {
	char *name;
	char *value;
};

#define MAX_MACROS 64
#define MAX_EXPAND_DEPTH 32

static struct macro_entry macro_map[MAX_MACROS];
static int macro_count;
static int expand_depth;

static struct macro_entry *lookup_macro(const char *name)
{
	int i;

	for (i = 0; i < macro_count; i++) {
		if (strcmp(macro_map[i].name, name) == 0)
			return &macro_map[i];
	}
	return NULL;
}

/* only object like macros -- the value stays raw text and gets relexed at expansion */
static void scan_define(const char *source, int *pos)
{
	char directive[64];
	int len;
	int start;
	int length;
	char *name;
	char *value;
	struct macro_entry *ent;

	(*pos)++;
	while (source[*pos] == ' ' || source[*pos] == '\t')
		(*pos)++;
	len = 0;
	while (isalpha(source[*pos]))
		directive[len++] = source[(*pos)++];
	directive[len] = '\0';
	if (strcmp(directive, "define") != 0) {
		fprintf(stderr, "redix: unknown directive '#%s'\n", directive);
		exit(1);
	}

	while (source[*pos] == ' ' || source[*pos] == '\t')
		(*pos)++;
	start = *pos;
	while (isalpha(source[*pos]) || isdigit(source[*pos])
			|| source[*pos] == '_')
		(*pos)++;
	length = *pos - start;
	name = malloc(length + 1);
	memcpy(name, &source[start], length);
	name[length] = '\0';

	while (source[*pos] == ' ' || source[*pos] == '\t')
		(*pos)++;
	start = *pos;
	while (source[*pos] != '\n' && source[*pos] != '\0')
		(*pos)++;
	length = *pos - start;
	while (length > 0 && isspace(source[start + length - 1]))
		length--;
	value = malloc(length + 1);
	memcpy(value, &source[start], length);
	value[length] = '\0';

	ent = lookup_macro(name);
	if (ent) {
		free(ent->value);
		ent->value = value;
		free(name);
		return;
	}
	if (macro_count >= MAX_MACROS) {
		fprintf(stderr, "redix: too many macros\n");
		exit(1);
	}
	macro_map[macro_count].name = name;
	macro_map[macro_count].value = value;
	macro_count++;
}

/* the value goes through the lexer again so macros can be built from other macros */
static int expand_macro(struct token **tokens, int ntokens, int *capacity)
{
	struct macro_entry *ent;
	struct token *sub;
	int sub_count;
	int i;

	if ((*tokens)[ntokens - 1].type != TOKEN_IDENTIFIER)
		return ntokens;
	ent = lookup_macro((*tokens)[ntokens - 1].value);
	if (!ent)
		return ntokens;

	if (expand_depth >= MAX_EXPAND_DEPTH) {
		fprintf(stderr, "redix: macro expansion too deep\n");
		exit(1);
	}
	expand_depth++;
	sub = lexer_tokenize(ent->value, &sub_count);
	expand_depth--;

	while (ntokens + sub_count >= *capacity) {
		*capacity *= 2;
		*tokens = realloc(*tokens, sizeof(struct token) * *capacity);
	}

	/* the subs EOF stays out since an EOF in the middle
	 * would end the token stream early */
	ntokens--;
	for (i = 0; i < sub_count - 1; i++)
		(*tokens)[ntokens++] = sub[i];
	free(sub);
	return ntokens;
}

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

static void scan_string(const char *source, int *pos,
		struct token *tokens, int *ntokens)
{
	char buf[4096];
	int len = 0;

	(*pos)++;
	while (source[*pos] != '"' && source[*pos] != '\0') {
		if (source[*pos] == '\\')
			buf[len++] = source[(*pos)++];
		buf[len++] = source[(*pos)++];
	}
	buf[len] = '\0';
	if (source[*pos] == '"')
		(*pos)++;
	tokens[(*ntokens)++] = (struct token){ TOKEN_STRING_LITERAL, strdup(buf) };
}

struct token *lexer_tokenize(const char *source, int *count)
{
	int capacity = 64;
	int ntokens = 0;
	int position = 0;
	struct token *tokens = malloc(sizeof(struct token) * capacity);
	char c;

	while (source[position] != '\0') {

		if (isspace(source[position])) {
			position++;
			continue;
		}

		if (source[position] == '/' && source[position + 1] == '/') {
			while (source[position] != '\0' && source[position] != '\n')
				position++;
			continue;
		}

		if (source[position] == '/' && source[position + 1] == '*') {
			position += 2;
			while (source[position] != '\0') {
				if (source[position] == '*' && source[position + 1] == '/') {
					position += 2;
					break;
				}
				position++;
			}
			continue;
		}

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
		case '?':
			tokens[ntokens++] = make_token(TOKEN_QUESTION, "?");
			position++;
			break;
		case ':':
			tokens[ntokens++] = make_token(TOKEN_COLON, ":");
			position++;
			break;
		case '[':
			tokens[ntokens++] = make_token(TOKEN_LBRACKET, "[");
			position++;
			break;
		case ']':
			tokens[ntokens++] = make_token(TOKEN_RBRACKET, "]");
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
			} else if (source[position + 1] == '>') {
				tokens[ntokens++] = make_token(TOKEN_ARROW, "->");
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
			} else if (source[position + 1] == '<') {
				tokens[ntokens++] = make_token(TOKEN_LSHIFT, "<<");
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
			} else if (source[position + 1] == '>') {
				tokens[ntokens++] = make_token(TOKEN_RSHIFT, ">>");
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
				tokens[ntokens++] = make_token(TOKEN_PIPE, "|");
				position++;
			}
			break;
		case '%':
			tokens[ntokens++] = make_token(TOKEN_PERCENT, "%");
			position++;
			break;
		case '^':
			tokens[ntokens++] = make_token(TOKEN_CARET, "^");
			position++;
			break;
		case '.':
			tokens[ntokens++] = make_token(TOKEN_DOT, ".");
			position++;
			break;
		case '"':
			scan_string(source, &position, tokens, &ntokens);
			break;
		case '#':
			scan_define(source, &position);
			break;
		default:
			if (isdigit(c)) {
				scan_number(source, &position, tokens, &ntokens);
			} else if (isalpha(c) || c == '_') {
				scan_identifier(source, &position, tokens, &ntokens);
				ntokens = expand_macro(&tokens, ntokens, &capacity);
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
