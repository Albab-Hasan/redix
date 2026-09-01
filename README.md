# redix

A C compiler written in C.

My goal for this project is to learn how compilers work by building a
functional compiler. So far I've implemented a lexer, a parser and
code generation. Currently it supports:

- arithmetic operators: `+`, `-`, `*`, `/`, `%`
- bitwise operators: `&`, `|`, `^`, `<<`, `>>`
- unary operators: `-`, `~`, `!`
- increment/decrement: prefix `++x`, `--x` and postfix `x++`, `x--`, on any lvalue rather than only a bare name, so `(*p)++`, `++(*p)`, `a[i]++`, `--a[i]`, `s.field++` and `p->field++` all work; when the target is a pointer it steps by its element size
- comparison operators: `<`, `>`, `<=`, `>=`, `==`, `!=`
- logical operators: `&&`, `||`
- ternary conditional: `cond ? a : b`
- local and global variable declarations, assignments, and references
- compound assignment: `+=`, `-=`, `*=`, `/=`, on the same set of lvalues, so `*p += 1`, `a[i] += 2`, `s.field *= 3` and `p->field -= 5` work. The target is duplicated into the expanded form, so a side effect written inside it happens twice
- `if`/`else` statements
- `while` loops
- `for` loops
- `break` and `continue`
- multiple functions with up to 6 named parameters; a call site can pass more than 6 arguments, with the extras going on the stack and `rsp` kept 16-byte aligned
- function prototypes: `int foo(int a);` forward-declares a function, enabling calls before the definition and mutual recursion
- `void` return type and bare `return;`
- pointer return types: `int *f()`, `char *f()`, `long *f()`, `void *f()` and `struct T *f()`, in definitions and in prototypes; each function records what its returned pointer points at, so a call result can be dereferenced (`*f(x)`), indexed (`f(x)[i]`), used in pointer arithmetic (`f(x) + 1`) and chained through a struct (`f(x)->field`) with the right element size; `void *` steps one byte at a time the way GCC treats `void *` arithmetic. This is what makes the libc allocation and string functions declarable: `void *malloc(long n);`, `char *strcpy(char *d, char *s);`. Multi-level returns like `int **f()` work too
- pointers: `int *p`, address-of `&x`, dereference `*p`, pointer parameters
- pointers to any depth: `int **q`, `char ***r`, with `**q` reads and writes, `&p` on a pointer variable, `q[i][j]` indexing and `q + 1` stepping by 8; usable as a local, a global, a parameter, a struct field, a return type and a cast. There is no limit on the depth
- `void *`: usable as a parameter (`int f(void *p)`), a local, a global and a struct field as well as a return type; steps one byte at a time in arithmetic the way GCC does, and converts to and from other pointer types without a cast. Dereferencing one reads a byte rather than raising an error
- pointer arithmetic: `p + n`, `p - n`, `p++`, `p--`, pointer difference, and `p[i]` indexing
- arrays: `int a[N]`, element access `a[i]`, brace initializers `int a[3] = {1, 2, 3}` and size-inferred `int a[] = {1, 2, 3}`, decay to pointer when passed to functions
- N-dimensional arrays: `int a[2][3]`, `char c[2][3][4]` as locals and globals, element access `a[i][j]`, row-major layout, nested brace initializers `int a[2][3] = {{1, 2, 3}, {4, 5, 6}}` (flat lists work too), outer size inferred from the initializer with `int a[][2] = {1, 2, 3, 4}`, and a partial index like `a[i]` gives the row address so it can be passed as `int *`
- arrays of pointers: `char *a[N]`, `int *a[N]` as locals and globals, with brace initializers `char *names[3] = {"aa", "bb", "cc"}`; each slot is 8 bytes, `a[i]` yields the pointer and `a[i][j]` indexes through it
- global arrays and pointers: `int a[N]`, `char buf[N]`, `int *p`, `char *s` at file scope, with initializers `int a[4] = {1, 2, 3, 4}`, size-inferred `int a[] = {1, 2}`, `char *s = "hi"` and `int *p = &g`; scalar initializers fold constant expressions at compile time (`2 * 3 + 1`, enum constants, `sizeof`), array elements left out of the list stay zero, and anything declared without an initializer is zeroed
- `char` type: declarations, assignments, arithmetic, arrays `char a[N]`, pointer `char *p`, function parameters
- character literals: `'a'`, `'0'`, `' '` and the escapes `\n`, `\t`, `\r`, `\0`, `\a`, `\b`, `\f`, `\v`, `\\`, `\'`, `\"`; the lexer decodes each one to its byte value and emits it as a number, so a character literal works anywhere a number does, including `case 'a':` labels and array initializers `char s[] = {'h', 'i', '\0'}`; octal `'\101'` and hex `'\x41'` escapes are not supported
- `sizeof(type)`: `sizeof(int)` / `sizeof(unsigned)` → 4, `sizeof(char)` → 1, `sizeof(long)` → 8, and any pointer type including `sizeof(int **)` → 8. `sizeof` applied to an expression rather than a type is not supported
- structs: `struct name { fields; }` definitions, local struct variables, member access and assignment via `.`, struct pointer declarations `struct T *p`, member access and assignment via `->`, struct pointer function parameters, `int`, `char`, `long` and pointer fields with real sizes and offsets (`char` packs to 1 byte, `int` aligns to 4, `long` and pointers align to 8, total size rounds up to the widest field)
- struct pointer fields: `struct node *next` inside a struct, including self-referential types, so linked lists and trees work
- member chains: `p->next->val`, `a[i].x`, `s.p->f`; every postfix step builds on the address of the previous one
- postfix on a parenthesized expression: `[`, `.`, `->`, `++` and `--` attach to whatever expression precedes them, so `(a)[0]`, `(p)[i]`, `(*t)[i]` on a `char **`, `(p)[0]++` and `(*q)++` on a pointer to a pointer all parse and index at the right width
- struct arrays: `struct T a[N]` as locals and globals, element access `a[i].field`, decay to a pointer when passed to functions
- global structs: `struct T g;`, `struct T *gp;` and `struct T a[N];` at file scope, with brace initializers laid out field by field with real padding, so the keyword-table shape `struct kw table[3] = { {"int", 11}, {"char", 22} };` works; pointer fields take a string literal, `&other` or `0`, and a struct array needs an explicit size since the parser does not know the field count
- struct pointer arithmetic: `p + n`, `p++`, `p--` step by the full struct size
- `sizeof(struct T)` reports the real laid-out size, `sizeof(struct T *)` → 8
- address of an element or member: `&a[i]`, `&s.field`, `&p->field`
- `switch`/`case`/`default`: integer switch with fallthrough, `break` exits the switch
- `do`/`while` loops: body runs at least once, `break` and `continue` work as expected
- enums: `enum name { A, B = 5, C };` at file scope, constants fold to numbers at parse time, usable in expressions and as `case` labels; the tag also works as a type — `enum name x;` as a local, global, parameter, return type, struct field or `for` init, plus `sizeof(enum name)` → 4 and the cast `(enum name)x`. An enum variable is an `int` in every respect, so the parser consumes the type and forgets it and no range or assignment checking happens. Enums must still be defined at file scope
- `#define NAME value`: object-like macros, expanded in the lexer, value can be any token sequence, macros can reference other macros, works as array sizes
- nested block scoping: `{ int x = 5; }` declares `x` only for the duration of the block; inner variables shadow outer ones with the same name and the outer name comes back when the block exits
- `unsigned int` and `unsigned char`: zero-extension on char load (`movzbl`), unsigned division (`divl`/`divq` with `xor edx`), unsigned right shift (`shrl`/`shrq`), unsigned comparison flags (`setb`/`seta`/`setbe`/`setae`)
- `long`: 64-bit integer, 64-bit arithmetic (`addq`/`subq`/`imulq`/`idivq`), `movq` loads and stores, works as local variables, function parameters, and return types
- struct value return: `struct T func(...)` returns the struct in `rax` (≤8 bytes) or `rax`:`rdx` (≤16 bytes) per the System V AMD64 ABI; caller unpacks into a local with `struct T v = func(...)`
- struct value parameters: `func(struct T p)` passes the struct in one register (≤8 bytes) or two registers (≤16 bytes); struct value args must be local variable references
- type casting: `(int)`, `(char)`, `(long)`, `(unsigned)`, `(unsigned char)`, `(void)`, `(enum name)` and a pointer to any of those at any depth (`(char *)`, `(int **)`), plus struct pointers `(struct T *)`; truncates or extends the value to the target type; `(char)` sign-extends from byte, `(unsigned char)` zero-extends, `(long)` sign-extends to 64 bits, and a pointer cast retypes the value so the arithmetic and load width that follow it change
- function pointers: `int (*fp)(int, int)` declarations, assignment from function names (`fp = add`), indirect calls (`fp(a, b)`), and function pointer parameters (`int apply(int (*fn)(int), int x)`); function names used as values decay to their address via `leaq`; indirect calls emit `call *%rax`
- variadic functions: `int sum(int n, ...)` definitions and prototypes, with `va_list`, `va_start(ap, last)`, `va_arg(ap, type)` and `va_end(ap)`; the prologue of a variadic function spills the six integer argument registers to a save area and `va_arg` walks that area first before falling through to the arguments the caller left on the stack, matching the System V AMD64 layout, so a `va_list` can be handed straight to a libc function like `vsnprintf` or `vprintf`; call sites zero `al` before calling anything variadic; floating point arguments are not supported since redix has no floating point types
- `static` and `const`: accepted wherever a type can start — functions, file-scope variables, locals, parameters, struct fields and casts (`static int f(const char *s)`, `const int x = 3`, `int const y = 4`, `char *const p`, `(const char *)p`) — and dropped by the parser. redix compiles one translation unit at a time so internal linkage changes nothing, and there is no write checking, so assigning to a `const` variable is silently allowed. A `static` local becomes an ordinary stack local, which only shows up if the local is written and expected to keep its value across calls
- empty parameter lists spelled `(void)`: `int main(void)` and `int f(void);` mean the same thing as `()`
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
