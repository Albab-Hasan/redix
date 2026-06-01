// expect: 3
int main()
{
	int x;
	int *p;
	int *q;
	x = 0;
	p = &x;
	q = p + 3;
	return q - p;
}
