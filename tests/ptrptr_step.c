// expect: 4
int main() {
	int a[2];
	int *rows[2];
	int **q;

	a[0] = 3;
	a[1] = 4;
	rows[0] = a;
	rows[1] = a + 1;
	q = rows;
	return **(q + 1);
}
