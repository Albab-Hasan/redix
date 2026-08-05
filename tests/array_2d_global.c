// expect: 30
int g[3][3];

int main() {
	g[2][1] = 30;
	g[0][0] = 5;
	return g[2][1];
}
