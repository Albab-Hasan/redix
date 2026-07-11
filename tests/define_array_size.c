// expect: 10
#define SIZE 4

int main()
{
	int a[SIZE];
	int i;
	int sum;

	sum = 0;
	for (i = 0; i < SIZE; i++) {
		a[i] = i + 1;
	}
	for (i = 0; i < SIZE; i++) {
		sum += a[i];
	}
	return sum;
}
