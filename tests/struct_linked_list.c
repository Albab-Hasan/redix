// expect: 50
struct node { int val; struct node *next; };
struct node pool[4];
int main()
{
	struct node *p;
	int i;
	int sum;

	for (i = 0; i < 4; i++)
		pool[i].val = (i + 1) * 5;
	for (i = 0; i < 3; i++)
		pool[i].next = &pool[i + 1];
	pool[3].next = 0;

	sum = 0;
	p = pool;
	while (p != 0) {
		sum = sum + p->val;
		p = p->next;
	}
	return sum;
}
