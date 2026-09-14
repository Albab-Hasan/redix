// expect: 17
int main() {
	char c;
	int i;
	long l;
	unsigned u;

	c = 1;
	i = 1;
	l = 1;
	u = 1;
	return sizeof(c) + sizeof(i) + sizeof(l) + sizeof(u);
}
