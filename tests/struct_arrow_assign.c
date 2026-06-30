// expect: 99
struct point { int x; int y; };
int main()
{
	struct point p;
	struct point *pp;
	pp = &p;
	pp->x = 99;
	pp->y = 1;
	return p.x;
}
