#ifndef CODEGEN_H
#define CODEGEN_H

#include "parser.h"

void codegen(struct ast_node *ast, FILE *out);

#endif
