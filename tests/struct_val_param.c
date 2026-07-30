// expect: 15
struct point { int x; int y; };

int sum(struct point p)
{
	return p.x + p.y;
}

int main()
{
	struct point a;
	a.x = 7;
	a.y = 8;
	return sum(a);
}
