// expect: 10
int main() {
	int a[5];
	int i;
	int sum;
	for (i = 0; i < 5; i++)
		a[i] = i;
	sum = 0;
	for (i = 0; i < 5; i++)
		sum = sum + a[i];
	return sum;
}
