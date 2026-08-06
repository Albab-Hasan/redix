// expect: 209
int main()
{
	char s[3];

	s[0] = 'h';
	s[1] = 'i';
	s[2] = '\0';
	return s[0] + s[1];
}
