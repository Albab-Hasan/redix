// expect: 60
struct point { int x; int y; };
int main()
{
	struct point ps[3];
	struct point *p;
	int total;

	ps[0].x = 10;
	ps[1].x = 20;
	ps[2].x = 30;
	p = ps;
	total = p->x;
	p++;
	total = total + p->x;
	p = p + 1;
	return total + p->x;
}
