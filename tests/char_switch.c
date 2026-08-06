// expect: 20
int classify(char c)
{
	switch (c) {
	case 'a':
		return 10;
	case 'b':
		return 20;
	default:
		return 30;
	}
}

int main()
{
	return classify('b');
}
