// expect: 72
struct mix { char c; int n; };
int main()
{
	struct mix m;
	struct mix *p;
	p = &m;
	p->c = 70;
	p->n = 2;
	return p->c + p->n;
}
