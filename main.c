#include <stdio.h>
#include <stdlib.h>
#include "lexer.h"
#include "parser.h"
#include "codegen.h"

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
	case NODE_UNARY:	printf("Unary: %s\n", node->value); break;
	case NODE_BINARY:	printf("Binary: %s\n", node->value); break;
	case NODE_DECLARATION:	printf("Declaration: %s\n", node->value); break;
	case NODE_ASSIGN:	printf("Assign: %s\n", node->value); break;
	case NODE_VAR:		printf("Var: %s\n", node->value); break;
	case NODE_COMPOUND:	printf("Compound\n"); break;
	case NODE_IF:		printf("If\n"); break;
	case NODE_WHILE:	printf("While\n"); break;
	case NODE_FOR:		printf("For\n"); break;
	case NODE_BREAK:	printf("Break\n"); break;
	case NODE_CONTINUE:	printf("Continue\n"); break;
	case NODE_CALL:		printf("Call: %s\n", node->value); break;
	case NODE_GLOBAL:	printf("Global: %s\n", node->value); break;
	case NODE_PREFIX_INC:	printf("PrefixInc: %s\n", node->value); break;
	case NODE_PREFIX_DEC:	printf("PrefixDec: %s\n", node->value); break;
	case NODE_POSTFIX_INC:	printf("PostfixInc: %s\n", node->value); break;
	case NODE_POSTFIX_DEC:	printf("PostfixDec: %s\n", node->value); break;
	case NODE_TERNARY:	printf("Ternary\n"); break;
	}

	for (i = 0; i < node->child_count; i++)
		print_ast(node->children[i], depth + 1);
}

int main(int argc, char **argv)
{
	FILE *file;
	FILE *outfile;
	long length;
	char *source;
	char *path = NULL;
	int count;
	int i;
	struct token *tokens;
	struct ast_node *ast;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] == 'I') {
			if (argv[i][2] != '\0')
				lexer_add_include_dir(&argv[i][2]);
			else if (i + 1 < argc)
				lexer_add_include_dir(argv[++i]);
		} else if (!path) {
			path = argv[i];
		}
	}

	if (!path) {
		fprintf(stderr, "Usage: redix <file.c> [-I dir]\n");
		return 1;
	}

	file = fopen(path, "r");
	if (!file) {
		fprintf(stderr, "redix: cannot open '%s'\n", path);
		return 1;
	}

	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	source = malloc(length + 1);
	fread(source, 1, length, file);
	source[length] = '\0';
	fclose(file);

	lexer_set_dir(path);
	tokens = lexer_tokenize(source, &count);
	free(source);

	/* debugging only */
	for (i = 0; i < count; i++)
		printf("%-18s %s\n", token_type_name(tokens[i].type), tokens[i].value);

	ast = parse(tokens, count);
	print_ast(ast, 0);

	outfile = fopen("out.s", "w");
	if (!outfile) {
		fprintf(stderr, "redix: cannot open out.s for writing\n");
		return 1;
	}
	codegen(ast, outfile);
	fclose(outfile);
	printf("wrote out.s\n");

	free_ast(ast);
	free(tokens);
	return 0;
}
