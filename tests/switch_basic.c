// expect: 2
int main()
{
	int x;
	x = 1;
	switch (x) {
	case 1:
		return 2;
	case 2:
		return 3;
	}
	return 0;
}
