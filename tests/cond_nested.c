// expect: 6
#define OUTER
#define INNER 6

int main()
{
#ifdef OUTER
#ifdef INNER
	return INNER;
#else
	return 1;
#endif
#else
	return 2;
#endif
}
