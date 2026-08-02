// expect: 4
int inc(int x) { return x + 1; }
int dbl(int x) { return x * 2; }
int main() {
	int (*fp)(int);
	fp = inc;
	int a;
	a = fp(1);
	fp = dbl;
	return fp(a);
}
