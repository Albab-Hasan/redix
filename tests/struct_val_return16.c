// expect: 10
struct quad { int a; int b; int c; int d; };

struct quad make_quad(int a, int b, int c, int d)
{
	struct quad q;
	q.a = a;
	q.b = b;
	q.c = c;
	q.d = d;
	return q;
}

int main()
{
	struct quad q = make_quad(1, 2, 3, 4);
	return q.a + q.b + q.c + q.d;
}
