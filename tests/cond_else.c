// expect: 3
int main()
{
#ifdef BAR
	return 2;
#else
	return 3;
#endif
}
