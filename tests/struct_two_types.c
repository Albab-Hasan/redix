// expect: 15
struct point { int x; int y; };
struct rgb { int r; int g; int b; };
int main()
{
	struct point p;
	struct rgb c;
	p.x = 5;
	p.y = 10;
	c.r = 3;
	c.g = 6;
	c.b = 1;
	return p.x + p.y;
}
