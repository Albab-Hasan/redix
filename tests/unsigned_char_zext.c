// expect: 1
int main()
{
	unsigned char c = 200;
	/* signed char would sign-extend to -56 making this false */
	if (c == 200)
		return 1;
	return 0;
}
