// expect: 15
int sum3(int *row)
{
	return row[0] + row[1] + row[2];
}

int main() {
	int a[2][3] = {{1, 2, 3}, {4, 5, 6}};

	return sum3(a[1]);
}
