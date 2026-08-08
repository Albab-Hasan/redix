// expect: 9
int vsnprintf(char *buf, long size, char *fmt, va_list ap);

int fmt(char *buf, char *f, ...) {
	va_list ap;
	int n;

	va_start(ap, f);
	n = vsnprintf(buf, 64, f, ap);
	va_end(ap);
	return n;
}

int main() {
	char buf[64];
	int n;

	n = fmt(buf, "%s is %d", "age", 41);
	if (buf[0] != 'a') return 1;
	if (buf[7] != '4') return 2;
	if (buf[8] != '1') return 3;
	return n;
}
