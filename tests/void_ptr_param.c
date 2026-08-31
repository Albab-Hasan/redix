// expect: 65
int first(void *p)
{
	char *c;

	c = p;
	return c[0];
}

int main() {
	void *v;

	v = "AB";
	return first(v);
}
