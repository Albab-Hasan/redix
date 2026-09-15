// expect: 12
#include "inc_guard.h"
#include "inc_guard.h"

int main()
{
	struct point p;

	p.x = GUARD_A;
	p.y = GUARD_B;
	return p.x + p.y;
}
