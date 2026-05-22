// expect: 10
int main() {
    int s = 0;
    int i = 0;
    for (i = 0; i < 5; i = i + 1)
        s = s + i;
    return s;
}
