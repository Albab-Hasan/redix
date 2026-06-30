// expect: 15
struct rect { int w; int h; };
int area(struct rect *r)
{
	return r->w * r->h;
}
int main()
{
	struct rect r;
	r.w = 3;
	r.h = 5;
	return area(&r);
}
