// expect: 30
int *nth(int *a, int n)
{
	return a + n;
}

int main()
{
	int x[4];
	int *p;

	x[0] = 10;
	x[1] = 20;
	x[2] = 30;
	x[3] = 40;

	p = nth(x, 1);
	if (*p != 20) return 1;
	if (*nth(x, 0) != 10) return 2;
	return *nth(x, 2);
}
