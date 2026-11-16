// Synthetic required separately versioned ABI implementation: extraction is disallowed by fixture contract.
int abi_v1(int input) {
    int a = input + 21;
    int b = a * 22;
    int c = b - 23;
    int d = c / 24;
    int e = d + 25;
    return e * 26;
}
