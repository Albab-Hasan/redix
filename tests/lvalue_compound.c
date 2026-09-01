// expect: 28
struct box { int n; };
int main()
{
	int a[2];
	int v;
	int *p;
	struct box s;
	struct box *b;

	v = 4;
	p = &v;
	*p += 6;
	a[0] = 1;
	a[0] += 2;
	s.n = 20;
	b = &s;
	b->n -= 5;
	s.n *= 1;
	return v + a[0] + b->n;
}
