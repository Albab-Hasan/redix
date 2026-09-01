// expect: 24
struct point { int x; char c; };
int main()
{
	struct point s;
	struct point *p;

	s.x = 10;
	s.c = 65;
	p = &s;
	s.x++;
	p->x++;
	s.c++;
	return p->x + p->c - 54;
}
