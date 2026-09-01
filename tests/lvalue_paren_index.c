// expect: 12
int main()
{
	int a[3];
	int *p;

	(a)[0] = 5;
	(a)[1] = 7;
	p = a;
	return (p)[0] + (p)[1];
}
