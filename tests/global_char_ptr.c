// expect: 105
char *s;
int main() {
	s = "hi";
	return s[0] * 0 + s[1];
}
