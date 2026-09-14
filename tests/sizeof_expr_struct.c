// expect: 44
struct pt {
	int x;
	int y;
};

int main() {
	struct pt p;
	struct pt *q;
	struct pt arr[3];

	return sizeof(p) + sizeof(q) + sizeof(arr) + sizeof(p.x);
}
