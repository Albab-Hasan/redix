// expect: 7
enum level { LOW = 1, MID = 2, HIGH = 4 };

enum level current = MID;
enum level backup;

int main()
{
	backup = HIGH;
	return current + backup + LOW;
}
