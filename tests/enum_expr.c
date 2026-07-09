// expect: 9
enum { ONE = 1, TWO, THREE };

int add(int a, int b)
{
	return a + b;
}

int main()
{
	return add(TWO, THREE) + ONE * 4;
}
