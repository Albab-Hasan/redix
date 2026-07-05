// expect: 3
int main()
{
	int x;
	int r;
	x = 1;
	r = 0;
	switch (x) {
	case 1:
		r = r + 1;
	case 2:
		r = r + 2;
	}
	return r;
}
