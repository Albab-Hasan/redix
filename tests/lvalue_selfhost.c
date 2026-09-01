// expect: 66
char peek(char *src, int *pos)
{
	return src[*pos];
}

char advance(char *src, int *pos)
{
	char c;

	c = src[*pos];
	(*pos)++;
	return c;
}

int main()
{
	char *src;
	int pos;

	src = "ABC";
	pos = 0;
	advance(src, &pos);
	if (peek(src, &pos) != 66)
		return 1;
	return advance(src, &pos);
}
