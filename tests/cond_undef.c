// expect: 8
#define GONE 1
#undef GONE

int main()
{
#ifdef GONE
	return 4;
#else
	return 8;
#endif
}
