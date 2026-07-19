// expect: 65
struct mix { char c; int n; };
int main()
{
	struct mix m;
	m.c = 65;
	m.n = 100;
	return m.c;
}
