// expect: 7
void *malloc(long n);

struct node {
	int v;
	struct node *next;
};

int main() {
	struct node *n;

	n = (struct node *)malloc(sizeof(*n));
	n->v = 7;
	n->next = 0;
	return n->v;
}
