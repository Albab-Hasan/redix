// expect: 14
int main()
{
	int a;
	int *p;
	int old;

	a = 5;
	p = &a;
	(*p)++;
	old = (*p)++;
	++(*p);
	return a + old;
}
