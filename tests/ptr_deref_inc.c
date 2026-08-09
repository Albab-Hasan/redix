// expect: 2
int main()
{
	char s[4];
	char *p;
	int ok;

	s[0] = 65;
	s[1] = 66;
	s[2] = 67;
	s[3] = 68;
	p = s;
	ok = 0;
	if (*p++ == 65)
		ok = ok + 1;
	if (*++p == 67)
		ok = ok + 1;
	return ok;
}
