// expect: 99
char *g[2][2] = { {"a", "b"}, {"c", "d"} };
int main()
{
	return g[1][0][0];
}
