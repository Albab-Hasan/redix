# redix

A C compiler written in C.

My goal for this project is to learn how compilers work by building a
functional compiler. So far I've implemented a lexer, a parser and
code generation. Currently it supports:

- arithmetic operators: `+`, `-`, `*`, `/`
- unary operators: `-`, `~`, `!`
- comparison operators: `<`, `>`, `<=`, `>=`, `==`, `!=`
- logical operators: `&&`, `||`
- local variable declarations, assignments, and references
- `if`/`else` statements
- `while` loops
- `for` loops
- `break` and `continue`
- multiple functions with up to 6 parameters and calls
- `void` return type and bare `return;`
- `//` line comments

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
