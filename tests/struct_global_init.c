// expect: 74
struct m { char c; int n; };
struct m v = {65, 9};
int main()
{
	return v.c + v.n;
}
