// expect: 22
enum { A = 5, B, C = 10, D };

int main()
{
	return A + B + D;
}
