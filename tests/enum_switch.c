// expect: 20
enum { RED, GREEN, BLUE };

int main()
{
	int c;
	c = GREEN;
	switch (c) {
	case RED:
		return 10;
	case GREEN:
		return 20;
	case BLUE:
		return 30;
	}
	return 0;
}
