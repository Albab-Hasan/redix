// expect: 7
void set(int **q, int v)
{
	**q = v;
}

int main() {
	int x;
	int *p;

	x = 0;
	p = &x;
	set(&p, 7);
	return x;
}
