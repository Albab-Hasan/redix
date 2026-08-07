// expect: 41
int p[2] = {1, 2};
int q[2] = {30, 40};
int *rows[2] = {p, q};
int main()
{
	return rows[1][1] + rows[0][0];
}
