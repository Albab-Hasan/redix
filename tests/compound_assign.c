// expect: 0
int main() {
	int x = 10;
	x += 5;
	x -= 3;
	x *= 2;
	x /= 4;
	if (x == 6) return 0;
	return 1;
}
