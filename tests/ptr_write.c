// expect: 7
int main() {
	int x = 0;
	int *p = &x;
	*p = 7;
	return x;
}
