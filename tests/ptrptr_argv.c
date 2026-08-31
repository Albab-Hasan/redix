// expect: 121
int second(char **a)
{
	return a[0][1];
}

int main() {
	char *rows[2];

	rows[0] = "xy";
	rows[1] = "zw";
	return second(rows);
}
