// expect: 21
int main() {
	int a[2][3] = {{1, 2, 3}, {4, 5, 6}};
	int i;
	int j;
	int sum;

	sum = 0;
	for (i = 0; i < 2; i = i + 1) {
		for (j = 0; j < 3; j = j + 1) {
			sum = sum + a[i][j];
		}
	}
	return sum;
}
