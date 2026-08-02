// expect: 10
int double_it(int x) { return x * 2; }
int apply(int (*fn)(int), int x) { return fn(x); }
int main() { return apply(double_it, 5); }
