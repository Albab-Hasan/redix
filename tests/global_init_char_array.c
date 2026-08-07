// expect: 105
char c[5] = {'h', 'i', 0};
int main()
{
	return c[1] + c[4];
}
