// expect: 1
#define FOO

int main()
{
#ifdef FOO
	return 1;
#else
	return 2;
#endif
}
