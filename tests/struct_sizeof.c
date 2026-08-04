// expect: 26
struct small { char a; char b; };
struct mixed { char c; int n; long l; };
int main()
{
	return sizeof(struct small) + sizeof(struct mixed) + sizeof(struct small *);
}
