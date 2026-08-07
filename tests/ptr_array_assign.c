// expect: 99
int main()
{
	char *a[2];
	a[0] = "ab";
	a[1] = "cd";
	return a[1][0];
}
