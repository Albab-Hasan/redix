// expect: 15
int sum(int *a, int n) {
	int s = 0;
	int i;
	for (i = 0; i < n; i++)
		s = s + a[i];
	return s;
}

int main() {
	int a[5];
	a[0] = 1;
	a[1] = 2;
	a[2] = 3;
	a[3] = 4;
	a[4] = 5;
	return sum(a, 5);
}
