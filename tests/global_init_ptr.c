// expect: 111
int g = 7;
int *p = &g;
char *msg = "hey";
int main()
{
	return *p + msg[0];
}
