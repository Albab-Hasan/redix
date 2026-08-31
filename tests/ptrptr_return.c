// expect: 11
int g;
int *p;

int **hold()
{
	p = &g;
	return &p;
}

int main() {
	g = 11;
	return **hold();
}
