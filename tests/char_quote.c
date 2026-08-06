// expect: 73
int main()
{
	char a[3] = {'\'', '"', '\0'};

	return a[0] + a[1];
}
