// expect: 99
char *skip(char *s, int n)
{
	return s + n;
}

int main()
{
	char buf[4];

	buf[0] = 'a';
	buf[1] = 'b';
	buf[2] = 'c';
	buf[3] = 0;

	if (*skip(buf, 0) != 'a') return 1;
	if (*(skip(buf, 1) + 1) != 'c') return 2;
	if (*skip(buf, 1) != 'b') return 3;
	return 99;
}
