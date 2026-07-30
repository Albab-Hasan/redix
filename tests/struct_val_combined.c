// expect: 42
struct point { int x; int y; };

struct point scale(struct point p, int factor)
{
	struct point r;
	r.x = p.x * factor;
	r.y = p.y * factor;
	return r;
}

int main()
{
	struct point p;
	p.x = 3;
	p.y = 4;
	struct point s = scale(p, 6);
	return s.x + s.y;
}
