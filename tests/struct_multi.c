// expect: 10
struct pair { int a; int b; };
int main()
{
	struct pair x;
	struct pair y;
	x.a = 3;
	x.b = 4;
	y.a = 1;
	y.b = 2;
	return x.a + x.b + y.a + y.b;
}
