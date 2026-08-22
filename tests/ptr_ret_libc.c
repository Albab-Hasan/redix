// expect: 11
void *malloc(long n);
char *strcpy(char *d, char *s);
int strlen(char *s);

int main()
{
	char *p;

	p = malloc(32);
	strcpy(p, "hello world");
	if (p[0] != 'h') return 1;
	if (p[6] != 'w') return 2;
	return strlen(p);
}
