// expect: 60
struct node { int val; struct node *next; };
int main()
{
	struct node a;
	struct node b;
	struct node c;

	a.val = 10;
	b.val = 20;
	c.val = 30;
	a.next = &b;
	b.next = &c;
	c.next = &a;
	return a.val + a.next->val + a.next->next->val;
}
