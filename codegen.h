#ifndef CODEGEN_H
#define CODEGEN_H

#include "parser.h"

/* takes a ast and writes x86-64 assembly to the given file */
void codegen(struct ast_node *ast, FILE *out);

#endif
