// expect: 15
enum { LO = 3, HI = 9 };
int lim = HI - LO;
int tab[2] = {LO, HI};
int main()
{
	return lim + tab[1];
}
