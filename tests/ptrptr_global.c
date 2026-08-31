// expect: 5
int g;
int *p;
int **q;

int main() {
	g = 5;
	p = &g;
	q = &p;
	return **q;
}
