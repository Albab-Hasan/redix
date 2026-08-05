// expect: 138
int main() {
	int a[3][4];
	int i;
	int j;
	int sum;
	int *p;

	for (i = 0; i < 3; i = i + 1) {
		for (j = 0; j < 4; j = j + 1) {
			a[i][j] = i * 10 + j;
		}
	}
	sum = 0;
	p = a[0];
	for (i = 0; i < 12; i = i + 1)
		sum = sum + p[i];
	return sum;
}
