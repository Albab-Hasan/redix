// expect: 42
int main() {
	int x;
	int *p;
	int **q;

	x = 0;
	p = &x;
	q = &p;
	**q = 42;
	return **q;
}
