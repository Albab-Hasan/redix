// expect: 78
int main()
{
	int a[2];
	char b[2];

	a[0] = 3;
	b[0] = 65;
	a[0]++;
	a[1] = 9;
	--a[1];
	b[0]++;
	return a[0] + a[1] + b[0];
}
