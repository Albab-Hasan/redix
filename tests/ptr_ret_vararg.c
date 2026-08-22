// expect: 7
int vsnprintf(char *buf, long size, char *fmt, va_list ap);

char *fmt(char *buf, char *f, ...)
{
	va_list ap;

	va_start(ap, f);
	vsnprintf(buf, 64, f, ap);
	va_end(ap);
	return buf;
}

int main()
{
	char buf[64];

	if (*fmt(buf, "%d-%d", 4, 5) != '4') return 1;
	if (fmt(buf, "%s", "hey")[2] != 'y') return 2;
	return 7;
}
