// expect: 10
struct pair { char a; char b; int n; };
int main()
{
	struct pair p;
	p.a = 3;
	p.b = 7;
	p.n = 100;
	return p.a + p.b;
}
