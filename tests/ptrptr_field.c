// expect: 6
struct holder {
	int **pp;
};

int main() {
	struct holder h;
	int x;
	int *p;

	x = 6;
	p = &x;
	h.pp = &p;
	return **h.pp;
}
