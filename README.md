# redix

A C compiler written in C.

My goal for this project is to learn how compilers work by building a
functional compiler. So far I've implemented a lexer, a parser and
code generation. Currently it supports:

- arithmetic operators: `+`, `-`, `*`, `/`
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
- `void` return type and bare `return;`
- pointers: `int *p`, address-of `&x`, dereference `*p`, pointer parameters
- pointer arithmetic: `p + n`, `p - n`, `p++`, `p--`, pointer difference, and `p[i]` indexing
- arrays: `int a[N]`, element access `a[i]`, decay to pointer when passed to functions
- `char` type: declarations, assignments, arithmetic, arrays `char a[N]`, pointer `char *p`, function parameters
- `sizeof(type)`: `sizeof(int)` → 4, `sizeof(char)` → 1, `sizeof(int *)` / `sizeof(char *)` → 8
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
