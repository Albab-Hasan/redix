// expect: 3
struct node { int v; struct node *next; };
struct node tail = {2, 0};
struct node head = {1, &tail};
int main()
{
	return head.v + head.next->v;
}
