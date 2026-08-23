// expect: 42
static int counter = 40;

static int bump(int n)
{
	counter = counter + n;
	return counter;
}

int main()
{
	return bump(2);
}
