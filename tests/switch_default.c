// expect: 9
int main()
{
	int x;
	x = 5;
	switch (x) {
	case 1:
		return 1;
	case 2:
		return 2;
	default:
		return 9;
	}
	return 0;
}
