#!/bin/bash
pass=0
fail=0

for f in tests/*.c; do
    expected=$(grep -m1 '^// expect:' "$f" | sed 's|// expect: *||')
    if [ -z "$expected" ]; then
        echo "SKIP $f (no // expect: line)"
        continue
    fi

    ./redix "$f" > /dev/null 2>&1
    gcc -o out out.s > /dev/null 2>&1
    ./out
    actual=$?

    if [ "$actual" -eq "$expected" ]; then
        echo "PASS $f"
        pass=$((pass + 1))
    else
        echo "FAIL $f (expected $expected, got $actual)"
        fail=$((fail + 1))
    fi
done

echo ""
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
