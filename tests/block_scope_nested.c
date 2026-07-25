// expect: 10
int main()
{
	int x = 10;
	{
		int x = 20;
		{
			int x = 30;
		}
	}
	return x;
}
