// expect: 30
int a[3];
int *p;
int main() {
	a[0] = 10;
	a[1] = 20;
	a[2] = 30;
	p = a;
	p++;
	p = p + 1;
	return *p;
}
