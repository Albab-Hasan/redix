// expect: 42
int main()
{
	int x;
	int *p;
	x = 42;
	p = &x;
	p = p + 1;
	p = p - 1;
	return *p;
}
