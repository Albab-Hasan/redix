// expect: 9
int main()
{
	int a[3];
	int *p;
	int **q;

	a[0] = 5;
	a[1] = 7;
	a[2] = 9;
	p = a;
	q = &p;
	(*q)++;
	(*q)++;
	return **q;
}
