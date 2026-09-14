// expect: 112
int main() {
	int a[10];
	char b[20];
	int m[2][4];

	return sizeof(a) + sizeof(b) + sizeof(m) + sizeof(a[0]) + sizeof(m[0]);
}
