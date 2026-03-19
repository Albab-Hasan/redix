#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"

int main(int argc, char **argv) {
	FILE *file;
	long length;
	char *source;
	int count;
	struct token *tokens;
	int i;	

	if (argc < 2) {
		fprintf(stderr, "Usage: redix <file.c>\n");
		return 1;
	}

	file = fopen(argv[1], "r");

	/* turn the entire source file into a string */
	if (!file) {
		fprintf(stderr, "redix: cannot open '%s'\n", argv[1]);
		return 1;
	}
	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	source = malloc(length + 1);
	fread(source, 1, length, file);
	source[length] = '\0';
	fclose(file);

	/* tokenize */
	tokens = lexer_tokenize(source, &count);

	/* print tokens for debugging */
	for (i = 0; i < count; i++) {
		printf("%-18s %s\n", token_type_name(tokens[i].type), tokens[i].value);
	}

	free(source);

	return 0;
}
