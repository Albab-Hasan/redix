// expect: 7
int main() {
	int a[2][3][4];

	a[1][2][3] = 7;
	a[0][0][0] = 1;
	return a[1][2][3];
}
