// expect: 105
int second(char **t)
{
	return (*t)[1];
}

int main()
{
	char *s;
	char **t;

	s = "hi";
	t = &s;
	return second(t);
}
