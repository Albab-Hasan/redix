// expect: 10
int main()
{
	int x;
	int r;
	x = 2;
	r = 0;
	switch (x) {
	case 1:
		r = 5;
		break;
	case 2:
		r = 10;
		break;
	case 3:
		r = 15;
		break;
	}
	return r;
}
