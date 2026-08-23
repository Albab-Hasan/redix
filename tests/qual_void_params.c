// expect: 7
static int seven(void);

int main(void)
{
	return seven();
}

static int seven(void)
{
	return 7;
}
