// expect: 5
int main()
{
	int i;
	int sum;
	i = 0;
	sum = 0;
	do {
		i = i + 1;
		if (i % 2 == 0)
			continue;
		sum = sum + 1;
	} while (i < 10);
	return sum;
}
