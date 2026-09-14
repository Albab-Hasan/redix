// expect: 5
int main() {
	int i;
	int n;

	i = 1;
	n = sizeof(i++);
	return i + n;
}
