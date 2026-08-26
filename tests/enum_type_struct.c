// expect: 21
enum kind { NUM, STR };

struct token {
	enum kind k;
	int len;
};

int main()
{
	struct token toks[2];
	int i;
	int total;

	toks[0].k = NUM;
	toks[0].len = 3;
	toks[1].k = STR;
	toks[1].len = 17;
	total = 0;
	for (i = 0; i < 2; i++)
		total = total + toks[i].k + toks[i].len;
	return total;
}
