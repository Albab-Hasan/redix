// expect: 7
int *base(int *a)
{
	return a;
}

char *cbase(char *s)
{
	return s;
}

int main()
{
	int x[3];
	char c[3];

	x[0] = 1;
	x[1] = 7;
	x[2] = 3;
	c[0] = 'x';
	c[1] = 'y';
	c[2] = 'z';

	if (cbase(c)[2] != 'z') return 1;
	return base(x)[1];
}
