// expect: 5
int my_strlen(char *s) {
	int n = 0;
	while (s[n] != 0)
		n++;
	return n;
}
int main() {
	return my_strlen("hello");
}
