// expect: 13
int main()
{
	int x;
	int *p;
	x = 13;
	p = &x;
	return p[0];
}
