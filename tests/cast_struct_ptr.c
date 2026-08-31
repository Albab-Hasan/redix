// expect: 9
struct box {
	int a;
};

int main() {
	struct box s;
	struct box *q;
	char *raw;

	s.a = 9;
	raw = (char *)&s;
	q = (struct box *)raw;
	return q->a;
}
