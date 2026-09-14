// expect: 37
int main() {
	char *p;
	int *q;
	char **r;
	char buf[9];

	p = buf;
	q = 0;
	r = &p;
	return sizeof(p) + sizeof(*p) + sizeof(q) + sizeof(*q)
			+ sizeof(r) + sizeof(*r);
}
