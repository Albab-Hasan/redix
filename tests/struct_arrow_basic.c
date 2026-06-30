// expect: 42
struct point { int x; int y; };
int main()
{
	struct point p;
	struct point *pp;
	p.x = 42;
	p.y = 7;
	pp = &p;
	return pp->x;
}
