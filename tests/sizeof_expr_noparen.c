// expect: 17
int main() {
	int i;
	char *p;
	char b[4];

	i = 0;
	p = b;
	return sizeof i + sizeof *p + sizeof b + sizeof p;
}
