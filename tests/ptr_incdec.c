// expect: 99
int main()
{
	int x;
	int *p;
	x = 99;
	p = &x;
	p++;
	p--;
	return *p;
}
