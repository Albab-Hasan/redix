// expect: 30
static int sum(const int a, const int b)
{
	return a + b;
}

int slen(const char *s)
{
	int n;

	n = 0;
	while (*s != 0) {
		n = n + 1;
		s = s + 1;
	}
	return n;
}

int main()
{
	const int x = 10;
	int const y = 15;
	const char *msg = "hello";

	return sum(x, y) + slen(msg);
}
