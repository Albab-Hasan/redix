// expect: 100
int sum(int n, ...) {
	va_list ap;
	int i;
	int total;

	total = 0;
	va_start(ap, n);
	for (i = 0; i < n; i++)
		total += va_arg(ap, int);
	va_end(ap);
	return total;
}

int main() {
	return sum(4, 10, 20, 30, 40);
}
