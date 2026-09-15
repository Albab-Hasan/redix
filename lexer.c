#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer.h"

/* stamped onto every token as it is made so errors can point at real source */
static int current_line = 1;

static struct token make_token(enum token_type type, const char *value)
{
	struct token t;
	t.type = type;
	t.value = strdup(value);
	t.line = current_line;
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
	case TOKEN_ELLIPSIS:		return "TOKEN_ELLIPSIS";
	case TOKEN_VA_LIST:		return "TOKEN_VA_LIST";
	case TOKEN_VA_START:		return "TOKEN_VA_START";
	case TOKEN_VA_ARG:		return "TOKEN_VA_ARG";
	case TOKEN_VA_END:		return "TOKEN_VA_END";
	case TOKEN_STATIC:		return "TOKEN_STATIC";
	case TOKEN_CONST:		return "TOKEN_CONST";
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
	{ "va_list",  TOKEN_VA_LIST },
	{ "va_start", TOKEN_VA_START },
	{ "va_arg",   TOKEN_VA_ARG },
	{ "va_end",   TOKEN_VA_END },
	{ "static",   TOKEN_STATIC },
	{ "const",    TOKEN_CONST },
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

#define MAX_MACROS 128
#define MAX_EXPAND_DEPTH 32
#define MAX_INCLUDE_DIRS 16
#define MAX_INCLUDE_DEPTH 16
#define MAX_COND 32

#ifndef REDIX_INCLUDE
#define REDIX_INCLUDE "include"
#endif

static struct macro_entry macro_map[MAX_MACROS];
static int macro_count;
static int expand_depth;

static char *include_dirs[MAX_INCLUDE_DIRS];
static int include_dir_count;
static int include_depth;
static int cond_depth;

/* the dir of the file being lexed right now so a quoted include resolves beside it */
static char *current_dir;

static struct macro_entry *lookup_macro(const char *name)
{
	int i;

	for (i = 0; i < macro_count; i++) {
		if (strcmp(macro_map[i].name, name) == 0)
			return &macro_map[i];
	}
	return NULL;
}

static void read_word(const char *source, int *pos, char *out)
{
	int len = 0;

	while (source[*pos] == ' ' || source[*pos] == '\t')
		(*pos)++;
	while (isalpha(source[*pos]))
		out[len++] = source[(*pos)++];
	out[len] = '\0';
}

static void read_name(const char *source, int *pos, char *out)
{
	int len = 0;

	while (source[*pos] == ' ' || source[*pos] == '\t')
		(*pos)++;
	while (isalpha(source[*pos]) || isdigit(source[*pos])
			|| source[*pos] == '_')
		out[len++] = source[(*pos)++];
	out[len] = '\0';
}

/* only object like macros -- the value stays raw text and gets relexed at expansion */
static void scan_define(const char *source, int *pos)
{
	int start;
	int length;
	char *name;
	char *value;
	struct macro_entry *ent;

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
		fprintf(stderr, "redix: line %d: too many macros\n", current_line);
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
		fprintf(stderr, "redix: line %d: macro expansion too deep\n",
				current_line);
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

void lexer_add_include_dir(const char *dir)
{
	if (include_dir_count >= MAX_INCLUDE_DIRS) {
		fprintf(stderr, "redix: too many include dirs\n");
		exit(1);
	}
	include_dirs[include_dir_count++] = strdup(dir);
}

static char *dir_of(const char *path)
{
	const char *slash = strrchr(path, '/');
	char *dir;
	int len;

	if (!slash)
		return strdup(".");
	len = slash - path;
	dir = malloc(len + 1);
	memcpy(dir, path, len);
	dir[len] = '\0';
	return dir;
}

void lexer_set_dir(const char *path)
{
	free(current_dir);
	current_dir = dir_of(path);
}

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "r");
	long length;
	char *text;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	length = ftell(f);
	fseek(f, 0, SEEK_SET);
	text = malloc(length + 1);
	fread(text, 1, length, f);
	text[length] = '\0';
	fclose(f);
	return text;
}

static char *join_path(const char *dir, const char *name)
{
	char *path = malloc(strlen(dir) + strlen(name) + 2);

	sprintf(path, "%s/%s", dir, name);
	return path;
}

/* the quoted form looks beside the including file first the angle form never does */
static char *open_include(const char *name, int angled, char **found)
{
	char *path;
	char *text;
	int i;

	if (!angled) {
		path = join_path(current_dir ? current_dir : ".", name);
		text = read_file(path);
		if (text) {
			*found = path;
			return text;
		}
		free(path);
	}
	for (i = 0; i < include_dir_count; i++) {
		path = join_path(include_dirs[i], name);
		text = read_file(path);
		if (text) {
			*found = path;
			return text;
		}
		free(path);
	}
	path = join_path(REDIX_INCLUDE, name);
	text = read_file(path);
	if (text) {
		*found = path;
		return text;
	}
	free(path);
	return NULL;
}

/* the file gets lexed on its own then its tokens splice in like a macro body */
static int scan_include(const char *source, int *pos, struct token **tokens,
		int ntokens, int *capacity)
{
	char name[256];
	char *text;
	char *path;
	char *saved_dir;
	struct token *sub;
	int sub_count;
	int saved_line;
	int len = 0;
	int angled;
	char close;
	int i;

	while (source[*pos] == ' ' || source[*pos] == '\t')
		(*pos)++;
	angled = source[*pos] == '<';
	if (!angled && source[*pos] != '"') {
		fprintf(stderr, "redix: line %d: bad include\n", current_line);
		exit(1);
	}
	close = angled ? '>' : '"';
	(*pos)++;
	while (source[*pos] != close && source[*pos] != '\n'
			&& source[*pos] != '\0')
		name[len++] = source[(*pos)++];
	name[len] = '\0';
	if (source[*pos] != close) {
		fprintf(stderr, "redix: line %d: unterminated include\n", current_line);
		exit(1);
	}
	(*pos)++;

	if (include_depth >= MAX_INCLUDE_DEPTH) {
		fprintf(stderr, "redix: line %d: includes nested too deep\n",
				current_line);
		exit(1);
	}
	text = open_include(name, angled, &path);
	if (!text) {
		fprintf(stderr, "redix: line %d: cannot find '%s'\n", current_line, name);
		exit(1);
	}

	/* the nested call restarts line numbering so the outer count has to survive it */
	saved_line = current_line;
	saved_dir = current_dir;
	current_dir = dir_of(path);
	include_depth++;
	sub = lexer_tokenize(text, &sub_count);
	include_depth--;
	free(current_dir);
	current_dir = saved_dir;
	current_line = saved_line;
	free(text);
	free(path);

	while (ntokens + sub_count >= *capacity) {
		*capacity *= 2;
		*tokens = realloc(*tokens, sizeof(struct token) * *capacity);
	}
	/* the subs EOF stays out since an EOF in the middle would end the stream early */
	for (i = 0; i < sub_count - 1; i++)
		(*tokens)[ntokens++] = sub[i];
	free(sub);
	return ntokens;
}

static void push_cond(void)
{
	if (cond_depth >= MAX_COND) {
		fprintf(stderr, "redix: line %d: conditionals nested too deep\n",
				current_line);
		exit(1);
	}
	cond_depth++;
}

static void pop_cond(void)
{
	if (cond_depth == 0) {
		fprintf(stderr, "redix: line %d: stray endif\n", current_line);
		exit(1);
	}
	cond_depth--;
}

static void undef_macro(const char *name)
{
	struct macro_entry *ent = lookup_macro(name);

	if (!ent)
		return;
	free(ent->name);
	free(ent->value);
	/* lookup is a linear scan so moving the last entry into the hole is fine */
	*ent = macro_map[--macro_count];
}

/* an untaken branch never reaches the lexer so only the directive lines get read */
static int skip_branch(const char *source, int *pos, int want_else)
{
	char word[64];
	int depth = 0;
	int bol = 1;
	char c;

	while (source[*pos] != '\0') {
		c = source[*pos];
		if (c == '#' && bol) {
			(*pos)++;
			read_word(source, pos, word);
			if (strcmp(word, "ifdef") == 0 || strcmp(word, "ifndef") == 0) {
				depth++;
			} else if (strcmp(word, "endif") == 0) {
				if (depth == 0)
					return 0;
				depth--;
			} else if (want_else && depth == 0
					&& strcmp(word, "else") == 0) {
				return 1;
			}
			continue;
		}
		if (c == '\n') {
			current_line++;
			bol = 1;
		} else if (c != ' ' && c != '\t') {
			bol = 0;
		}
		(*pos)++;
	}
	fprintf(stderr, "redix: line %d: missing endif\n", current_line);
	exit(1);
}

static int scan_directive(const char *source, int *pos, struct token **tokens,
		int ntokens, int *capacity)
{
	char word[64];
	char name[256];
	int defined;
	int want;

	(*pos)++;
	read_word(source, pos, word);

	if (strcmp(word, "define") == 0) {
		scan_define(source, pos);
	} else if (strcmp(word, "undef") == 0) {
		read_name(source, pos, name);
		undef_macro(name);
	} else if (strcmp(word, "include") == 0) {
		ntokens = scan_include(source, pos, tokens, ntokens, capacity);
	} else if (strcmp(word, "ifdef") == 0 || strcmp(word, "ifndef") == 0) {
		read_name(source, pos, name);
		defined = lookup_macro(name) != NULL;
		want = strcmp(word, "ifdef") == 0;
		if (defined == want)
			push_cond();
		else if (skip_branch(source, pos, 1))
			push_cond();
	} else if (strcmp(word, "else") == 0) {
		/* reaching an else while lexing means the if half was the taken one */
		skip_branch(source, pos, 0);
		pop_cond();
	} else if (strcmp(word, "endif") == 0) {
		pop_cond();
	} else {
		fprintf(stderr, "redix: line %d: unknown directive '#%s'\n",
				current_line, word);
		exit(1);
	}
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
	tokens[(*ntokens)++] = (struct token){ TOKEN_NUMBER, number, current_line };
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
	tokens[(*ntokens)++] = (struct token){ type, word, current_line };
}

/* only the named escapes so far no octal or hex forms */
static int decode_escape(char c)
{
	switch (c) {
	case 'n':	return '\n';
	case 't':	return '\t';
	case 'r':	return '\r';
	case '0':	return '\0';
	case 'a':	return '\a';
	case 'b':	return '\b';
	case 'f':	return '\f';
	case 'v':	return '\v';
	case '\\':	return '\\';
	case '\'':	return '\'';
	case '"':	return '"';
	}
	fprintf(stderr, "redix: line %d: unknown escape '\\%c'\n", current_line, c);
	exit(1);
}

/* the decoded byte becomes a plain number token so the parser
 * needs no char literal case and case labels keep working */
static void scan_char(const char *source, int *pos,
		struct token *tokens, int *ntokens)
{
	char buf[16];
	int value;

	(*pos)++;
	if (source[*pos] == '\\') {
		(*pos)++;
		value = decode_escape(source[(*pos)++]);
	} else {
		value = source[(*pos)++];
	}
	if (source[*pos] != '\'') {
		fprintf(stderr, "redix: line %d: unterminated character literal\n",
				current_line);
		exit(1);
	}
	(*pos)++;
	sprintf(buf, "%d", value);
	tokens[(*ntokens)++] = (struct token){ TOKEN_NUMBER, strdup(buf), current_line };
}

/* escapes stay raw text since the assembler decodes them in .string */
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
	tokens[(*ntokens)++] = (struct token){ TOKEN_STRING_LITERAL, strdup(buf), current_line };
}

struct token *lexer_tokenize(const char *source, int *count)
{
	int capacity = 64;
	int ntokens = 0;
	int position = 0;
	int cond_start = cond_depth;
	struct token *tokens = malloc(sizeof(struct token) * capacity);
	char c;

	/* a nested call is a macro body being relexed so the outer line must survive */
	if (expand_depth == 0)
		current_line = 1;

	while (source[position] != '\0') {

		if (isspace(source[position])) {
			if (source[position] == '\n')
				current_line++;
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
				if (source[position] == '\n')
					current_line++;
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
			if (source[position + 1] == '.' && source[position + 2] == '.') {
				tokens[ntokens++] = make_token(TOKEN_ELLIPSIS, "...");
				position += 3;
				break;
			}
			tokens[ntokens++] = make_token(TOKEN_DOT, ".");
			position++;
			break;
		case '"':
			scan_string(source, &position, tokens, &ntokens);
			break;
		case '\'':
			scan_char(source, &position, tokens, &ntokens);
			break;
		case '#':
			ntokens = scan_directive(source, &position, &tokens,
					ntokens, &capacity);
			break;
		default:
			if (isdigit(c)) {
				scan_number(source, &position, tokens, &ntokens);
			} else if (isalpha(c) || c == '_') {
				scan_identifier(source, &position, tokens, &ntokens);
				ntokens = expand_macro(&tokens, ntokens, &capacity);
			} else {
				fprintf(stderr, "redix: line %d: unexpected character '%c'\n",
						current_line, c);
				exit(1);
			}
			break;
		}
	}

	if (cond_depth != cond_start) {
		fprintf(stderr, "redix: line %d: unterminated conditional\n",
				current_line);
		exit(1);
	}

	tokens[ntokens++] = make_token(TOKEN_EOF, "EOF");
	*count = ntokens;
	return tokens;
}
