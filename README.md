# redix

A C compiler written in C.

My goal for this project is to learn how compilers work by building a
functional compiler. So far I've implemented a lexer, a parser and
code generation. Currently it supports:

- arithmetic operators: `+`, `-`, `*`, `/`, `%`
- bitwise operators: `&`, `|`, `^`, `<<`, `>>`
- unary operators: `-`, `~`, `!`
- increment/decrement: prefix `++x`, `--x` and postfix `x++`, `x--`
- comparison operators: `<`, `>`, `<=`, `>=`, `==`, `!=`
- logical operators: `&&`, `||`
- ternary conditional: `cond ? a : b`
- local and global variable declarations, assignments, and references
- compound assignment: `+=`, `-=`, `*=`, `/=`
- `if`/`else` statements
- `while` loops
- `for` loops
- `break` and `continue`
- multiple functions with up to 6 parameters and calls
- function prototypes: `int foo(int a);` forward-declares a function, enabling calls before the definition and mutual recursion
- `void` return type and bare `return;`
- pointers: `int *p`, address-of `&x`, dereference `*p`, pointer parameters
- pointer arithmetic: `p + n`, `p - n`, `p++`, `p--`, pointer difference, and `p[i]` indexing
- arrays: `int a[N]`, element access `a[i]`, decay to pointer when passed to functions
- global arrays and pointers: `int a[N]`, `char buf[N]`, `int *p`, `char *s` at file scope, zero-initialized
- `char` type: declarations, assignments, arithmetic, arrays `char a[N]`, pointer `char *p`, function parameters
- `sizeof(type)`: `sizeof(int)` → 4, `sizeof(char)` → 1, `sizeof(int *)` / `sizeof(char *)` → 8
- structs: `struct name { fields; }` definitions, local struct variables, member access and assignment via `.`, struct pointer declarations `struct T *p`, member access and assignment via `->`, struct pointer function parameters, `int` and `char` fields with real sizes and offsets (`char` packs to 1 byte, `int` aligns to 4, total size rounds up to the widest field)
- `switch`/`case`/`default`: integer switch with fallthrough, `break` exits the switch
- `do`/`while` loops: body runs at least once, `break` and `continue` work as expected
- enums: `enum name { A, B = 5, C };` at file scope, constants fold to numbers at parse time, usable in expressions and as `case` labels
- `#define NAME value`: object-like macros, expanded in the lexer, value can be any token sequence, macros can reference other macros, works as array sizes
- nested block scoping: `{ int x = 5; }` declares `x` only for the duration of the block; inner variables shadow outer ones with the same name and the outer name is restored when the block exits
- `//` line comments and `/* */` block comments

## Building

```
make
```

## Usage

```
./redix input.c
gcc -o out out.s
./out
```

## Tests

```
make test
```

Test files live in `tests/`. Each file has a `// expect: N` comment on the
first line indicating the expected exit code.

## License

MIT
