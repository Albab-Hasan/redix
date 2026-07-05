// expect: 7
int main()
{
	int x;
	int r;
	x = 3;
	r = 0;
	switch (x + 1) {
	case 4:
		r = 7;
		break;
	case 5:
		r = 8;
		break;
	}
	return r;
}
