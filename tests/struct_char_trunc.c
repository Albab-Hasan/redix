// expect: 44
struct mix { char c; int n; };
int main()
{
	struct mix m;
	m.n = 7;
	m.c = 300;
	return m.c + m.n - 7;
}
