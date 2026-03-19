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
	t.value = strdup(value);	/* duplicate the string */

	return t;
}

const char *token_type_name(enum token_type type)
{
	switch (type) {
	case TOKEN_INT:		return "TOKEN_INT";
	case TOKEN_RETURN:	return "TOKEN_RETURN";
	case TOKEN_IDENTIFIER:	return "TOKEN_IDENTIFIER";
	case TOKEN_NUMBER:	return "TOKEN_NUMBER";
	case TOKEN_LPAREN:	return "TOKEN_LPAREN";
	case TOKEN_RPAREN:	return "TOKEN_RPAREN";
	case TOKEN_LBRACE:	return "TOKEN_LBRACE";
	case TOKEN_RBRACE:	return "TOKEN_RBRACE";
	case TOKEN_SEMICOLON:	return "TOKEN_SEMICOLON";
	case TOKEN_EOF:		return "TOKEN_EOF";
	default:		return "UNKNOWN";
	}
}

struct token *lexer_tokenize(const char *source, int *count)
{
	int capacity = 64;
	struct token *tokens = malloc(sizeof(struct token) * capacity);
	int ntokens = 0;
	int position = 0;

	while (source[position] != '\0') {

		/* skip whitespaces */
		if (isspace(source[position])) {
			position++;
			continue;
		}

		/* single char tokens */
		if (source[position] == '(') {
			tokens[ntokens++] = make_token(TOKEN_LPAREN, "(");
			position++;
		} else if (source[position] == ')') {
			tokens[ntokens++] = make_token(TOKEN_RPAREN, ")");
			position++;
		} else if (source[position] == '{') {
			tokens[ntokens++] = make_token(TOKEN_LBRACE, "{");
			position++;
		} else if (source[position] == '}') {
			tokens[ntokens++] = make_token(TOKEN_RBRACE, "}");
			position++;
		} else if (source[position] == ';') {
			tokens[ntokens++] = make_token(TOKEN_SEMICOLON, ";");
			position++;
		}

		/* 
		 * numbers
		 * when a number comes up, keep going until the number ends 
		 */
		else if (isdigit(source[position])) {
			int start, length;
			char *number;	

			start = position;
			while (isdigit(source[position])) {
				position++;
			}
			/* extract the substring */
			length = position - start;	
			number = malloc(length + 1);
			strncpy(number, &source[start], length);
			number[length] = '\0';

			tokens[ntokens++] = make_token(TOKEN_NUMBER, number);
			free(number);
		}

		/* keywords and identifiers */
		/* for now im thinking letters and underscores start a word */
		else if (isalpha(source[position]) || source[position] == '_') {
			int start;
			int length;
			char *word;

			start = position;
			while (isalpha(source[position]) || isdigit(source[position])
				|| source[position] == '_') {
				position++;
			}
			/* extract the word */
			length = position - start;
			word = malloc(length + 1);
			strncpy(word, &source[start], length);
			word[length] = '\0';

			/* find out if its a keyword or a identifier */
			/* for now just int, return and identifiers */
			if (strcmp(word, "int") == 0) {
				tokens[ntokens++] = make_token(TOKEN_INT, word);
			} else if (strcmp(word, "return") == 0) {
				tokens[ntokens++] = make_token(TOKEN_RETURN, word);
			} else {
				tokens[ntokens++] = make_token(TOKEN_IDENTIFIER, word);
			}
			free(word);
		}

		/* unknown characters */
		else {
			fprintf(stderr, "redix: unexpected character '%c'\n", source[position]);
			exit(1);
		}

		/* grow the array if needed */
		if (ntokens >= capacity) {
			capacity *= 2;
			tokens = realloc(tokens, sizeof(struct token) * capacity);
		}
	}

	/* add EOF token at the end */
	tokens[ntokens++] = make_token(TOKEN_EOF, "EOF");

	*count = ntokens;
	return tokens;
}
