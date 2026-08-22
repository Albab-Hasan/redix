// expect: 6
char *tail(char *s);

int main()
{
	char buf[4];

	buf[0] = 'a';
	buf[1] = 'b';
	buf[2] = 6;
	buf[3] = 'z';

	if (*tail(buf) != 6) return 1;
	return 6;
}

char *tail(char *s)
{
	return s + 2;
}
