// expect: 7
struct point { int x; int y; };

struct point make_point(int x, int y)
{
	struct point p;
	p.x = x;
	p.y = y;
	return p;
}

int main()
{
	struct point p = make_point(3, 4);
	return p.x + p.y;
}
