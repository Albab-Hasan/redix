// expect: 12
struct entry {
	const char *word;
	const int value;
};

static struct entry table[2] = { { "int", 5 }, { "char", 7 } };

int main(void)
{
	return table[0].value + table[1].value;
}
