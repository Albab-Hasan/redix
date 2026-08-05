// expect: 99
int main() {
	char c[2][3];

	c[1][1] = 99;
	c[0][0] = 1;
	return c[1][1];
}
