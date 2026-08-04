// expect: 42
struct counter { int hits; char tag; };
struct counter g;
struct counter *gp;
int bump(int n)
{
	g.hits = g.hits + n;
	return g.hits;
}
int main()
{
	g.hits = 0;
	g.tag = 2;
	bump(20);
	bump(20);
	gp = &g;
	return gp->hits + gp->tag;
}
