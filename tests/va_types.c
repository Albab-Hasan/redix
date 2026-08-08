// expect: 77
int g[3] = { 10, 20, 30 };

long lsum(int n, ...) {
	va_list ap;
	long total;
	int i;

	total = 0;
	va_start(ap, n);
	for (i = 0; i < n; i++)
		total += va_arg(ap, long);
	va_end(ap);
	return total;
}

int firstchar(int n, ...) {
	va_list ap;
	char *s;

	va_start(ap, n);
	s = va_arg(ap, char*);
	va_end(ap);
	return s[0];
}

int second(int n, ...) {
	va_list ap;
	int *p;

	va_start(ap, n);
	p = va_arg(ap, int*);
	va_end(ap);
	return p[1];
}

int main() {
	if (lsum(3, 1000000000, 1000000000, 1000000000) != 3000000000) return 1;
	if (firstchar(1, "zebra") != 122) return 2;
	if (second(1, g) != 20) return 3;
	return 77;
}
