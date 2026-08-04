// expect: 36
struct point { int x; int y; };
int main()
{
	struct point ps[3];
	int i;
	int total;

	for (i = 0; i < 3; i++) {
		ps[i].x = i + 1;
		ps[i].y = i * 10;
	}
	total = 0;
	for (i = 0; i < 3; i++)
		total = total + ps[i].x + ps[i].y;
	return total;
}
