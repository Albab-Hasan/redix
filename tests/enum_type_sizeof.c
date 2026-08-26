// expect: 16
enum flag { OFF, ON };

int main()
{
	enum flag f;
	enum flag *p;
	int n;

	f = ON;
	p = &f;
	n = 0;
	for (f = OFF; f < 2; f++)
		n = n + f;
	return sizeof(enum flag) + sizeof(enum flag *) + *p + n + (enum flag)1;
}
