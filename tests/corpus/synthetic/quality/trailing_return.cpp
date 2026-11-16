// Synthetic complex function with unsupported trailing-return syntax: known false negative.
auto trailing_decisions(int x) -> int {
    if (x > 1) --x;
    if (x > 2) --x;
    if (x > 3) --x;
    if (x > 4) --x;
    if (x > 5) --x;
    if (x > 6) --x;
    if (x > 7) --x;
    if (x > 8) --x;
    if (x > 9) --x;
    if (x > 10) --x;
    return x;
}
