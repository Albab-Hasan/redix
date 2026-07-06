// expect: 3
int main()
{
	int i;
	i = 0;
	do {
		i = i + 1;
		if (i == 3)
			break;
	} while (i < 10);
	return i;
}
