// expect: 5
int main()
{
#ifdef NOTHING
	this text never reaches the lexer @@@ !!!
#endif
	return 5;
}
