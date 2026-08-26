// expect: 12
enum op { ADD, MUL };

enum op flip(enum op o)
{
	if (o == ADD)
		return MUL;
	return ADD;
}

int apply(enum op o, int a, int b)
{
	if (o == ADD)
		return a + b;
	return a * b;
}

int main()
{
	enum op o;

	o = ADD;
	return apply(flip(o), 3, 4);
}
