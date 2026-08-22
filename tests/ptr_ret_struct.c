// expect: 42
struct node { int val; struct node *next; };
struct node pool[3];

struct node *at(int i)
{
	return &pool[i];
}

struct node *step(struct node *p)
{
	return p->next;
}

int main()
{
	pool[0].val = 7;
	pool[1].val = 42;
	pool[2].val = 9;
	pool[0].next = &pool[1];
	pool[1].next = &pool[2];
	pool[2].next = 0;

	if (at(0)->val != 7) return 1;
	if (step(at(0))->val != 42) return 2;
	if (at(0)->next->next->val != 9) return 3;
	return at(1)->val;
}
