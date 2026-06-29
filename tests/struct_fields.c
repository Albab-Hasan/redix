// expect: 7
struct point { int x; int y; };
int main()
{
	struct point p;
	p.x = 3;
	p.y = 4;
	return p.x + p.y;
}
