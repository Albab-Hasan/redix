// expect: 10
static const char *names[3] = { "aa", "bbb", "cccc" };

static int slen(const char *s)
{
	int n;

	n = 0;
	while (*s != 0) {
		n = n + 1;
		s = s + 1;
	}
	return n;
}

int main(void)
{
	char buf[4];
	char *const p = buf;
	int i;
	int total;

	total = 0;
	for (i = 0; i < 3; i = i + 1)
		total = total + slen(names[i]);
	p[0] = 'x';
	p[1] = 0;
	return total + slen((const char *)p);
}
