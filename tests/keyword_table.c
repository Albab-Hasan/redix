// expect: 22
struct kw { char *word; int tok; };
struct kw table[3] = { {"int", 11}, {"char", 22}, {"return", 33} };
int strcmp(char *a, char *b);
int lookup(char *w)
{
	int i;
	for (i = 0; i < 3; i++)
		if (strcmp(w, table[i].word) == 0)
			return table[i].tok;
	return 0;
}
int main()
{
	return lookup("char");
}
