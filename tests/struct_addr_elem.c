// expect: 25
struct pair { int a; int b; };
int sum(struct pair *p)
{
	return p->a + p->b;
}
int main()
{
	struct pair ps[2];
	int *ip;

	ps[0].a = 1;
	ps[0].b = 2;
	ps[1].a = 10;
	ps[1].b = 12;
	ip = &ps[0].b;
	*ip = 2;
	return sum(&ps[0]) + sum(&ps[1]);
}
