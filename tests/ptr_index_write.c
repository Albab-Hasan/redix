// expect: 5
int main()
{
	int x;
	int *p;
	x = 0;
	p = &x;
	p[0] = 5;
	return x;
}
