// expect: 5
void set(int *p, int v) {
	*p = v;
}

int main() {
	int x = 0;
	set(&x, 5);
	return x;
}
