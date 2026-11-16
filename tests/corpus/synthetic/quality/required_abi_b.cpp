// Synthetic required separately versioned ABI implementation: lexical duplicate is a known false positive.
int abi_v2(int input) {
    int a = input + 21;
    int b = a * 22;
    int c = b - 23;
    int d = c / 24;
    int e = d + 25;
    return e * 26;
}
