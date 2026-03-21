#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"

/* print the ast */
void print_ast(struct ast_node *node, int depth)
{
	int i;

	for (i = 0; i < depth; i++)
		printf(" ");

	switch (node->type) {
	case NODE_PROGRAM:	printf("Program\n"); break;
	case NODE_FUNCTION:	printf("Function: %s\n", node->value); break;
	case NODE_RETURN:	printf("Return\n"); break;
	case NODE_NUMBER:	printf("Number: %s\n", node->value); break;	
	}

	for (i = 0; i < node->child_count; i++)
		print_ast(node->children[i], depth + 1);
}

int main(int argc, char **argv) {
	FILE *file;
	long length;
	char *source;
	int count;
	int i;	
	struct token *tokens;	
	struct ast_node *ast;

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

	ast = parse(tokens, count);
	print_ast(ast, 0);

	{
		FILE *outfile;
		outfile = fopen("out.s", "w");
		if (!outfile) {
			fprintf(stderr, "redix: cannot open out.s for writing\n");
			return 1;
		}
		codegen(ast, outfile);
		fclose(outfile);
		printf("wrote out.s\n");
	}

	free_ast(ast);
	free(tokens);

	return 0;
}
