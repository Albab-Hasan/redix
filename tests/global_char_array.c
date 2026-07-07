// expect: 65
char buf[8];
int main() {
	buf[0] = 65;
	buf[7] = 1;
	return buf[0] * buf[7];
}
