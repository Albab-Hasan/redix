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
- arrays: `int a[N]`, element access `a[i]`, brace initializers `int a[3] = {1, 2, 3}` and size-inferred `int a[] = {1, 2, 3}`, decay to pointer when passed to functions
- N-dimensional arrays: `int a[2][3]`, `char c[2][3][4]` as locals and globals, element access `a[i][j]`, row-major layout, nested brace initializers `int a[2][3] = {{1, 2, 3}, {4, 5, 6}}` (flat lists work too), outer size inferred from the initializer with `int a[][2] = {1, 2, 3, 4}`, and a partial index like `a[i]` gives the row address so it can be passed as `int *`
- global arrays and pointers: `int a[N]`, `char buf[N]`, `int *p`, `char *s` at file scope, zero-initialized
- `char` type: declarations, assignments, arithmetic, arrays `char a[N]`, pointer `char *p`, function parameters
- character literals: `'a'`, `'0'`, `' '` and the escapes `\n`, `\t`, `\r`, `\0`, `\a`, `\b`, `\f`, `\v`, `\\`, `\'`, `\"`; the lexer decodes each one to its byte value and emits it as a number, so a character literal works anywhere a number does, including `case 'a':` labels and array initializers `char s[] = {'h', 'i', '\0'}`; octal `'\101'` and hex `'\x41'` escapes are not supported
- `sizeof(type)`: `sizeof(int)` → 4, `sizeof(char)` → 1, `sizeof(int *)` / `sizeof(char *)` → 8
- structs: `struct name { fields; }` definitions, local struct variables, member access and assignment via `.`, struct pointer declarations `struct T *p`, member access and assignment via `->`, struct pointer function parameters, `int`, `char`, `long` and pointer fields with real sizes and offsets (`char` packs to 1 byte, `int` aligns to 4, `long` and pointers align to 8, total size rounds up to the widest field)
- struct pointer fields: `struct node *next` inside a struct, including self-referential types, so linked lists and trees work
- member chains: `p->next->val`, `a[i].x`, `s.p->f`; every postfix step builds on the address of the previous one
- struct arrays: `struct T a[N]` as locals and globals, element access `a[i].field`, decay to a pointer when passed to functions
- global structs: `struct T g;`, `struct T *gp;` and `struct T a[N];` at file scope, zero-initialized
- struct pointer arithmetic: `p + n`, `p++`, `p--` step by the full struct size
- `sizeof(struct T)` reports the real laid-out size, `sizeof(struct T *)` → 8
- address of an element or member: `&a[i]`, `&s.field`, `&p->field`
- `switch`/`case`/`default`: integer switch with fallthrough, `break` exits the switch
- `do`/`while` loops: body runs at least once, `break` and `continue` work as expected
- enums: `enum name { A, B = 5, C };` at file scope, constants fold to numbers at parse time, usable in expressions and as `case` labels
- `#define NAME value`: object-like macros, expanded in the lexer, value can be any token sequence, macros can reference other macros, works as array sizes
- nested block scoping: `{ int x = 5; }` declares `x` only for the duration of the block; inner variables shadow outer ones with the same name and the outer name comes back when the block exits
- `unsigned int` and `unsigned char`: zero-extension on char load (`movzbl`), unsigned division (`divl`/`divq` with `xor edx`), unsigned right shift (`shrl`/`shrq`), unsigned comparison flags (`setb`/`seta`/`setbe`/`setae`)
- `long`: 64-bit integer, 64-bit arithmetic (`addq`/`subq`/`imulq`/`idivq`), `movq` loads and stores, works as local variables, function parameters, and return types
- struct value return: `struct T func(...)` returns the struct in `rax` (≤8 bytes) or `rax`:`rdx` (≤16 bytes) per the System V AMD64 ABI; caller unpacks into a local with `struct T v = func(...)`
- struct value parameters: `func(struct T p)` passes the struct in one register (≤8 bytes) or two registers (≤16 bytes); struct value args must be local variable references
- type casting: `(int)`, `(char)`, `(long)`, `(unsigned)`, `(unsigned char)`, `(int *)`, `(char *)`; truncates or extends the value to the target type; `(char)` sign-extends from byte, `(unsigned char)` zero-extends, `(long)` sign-extends to 64 bits
- function pointers: `int (*fp)(int, int)` declarations, assignment from function names (`fp = add`), indirect calls (`fp(a, b)`), and function pointer parameters (`int apply(int (*fn)(int), int x)`); function names used as values decay to their address via `leaq`; indirect calls emit `call *%rax`
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
