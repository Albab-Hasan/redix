// expect: 3
enum color { RED, GREEN, BLUE };

int main()
{
	enum color c;
	enum color d;

	c = GREEN;
	d = BLUE;
	return c + d;
}
