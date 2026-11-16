// Synthetic four-dimensional aggregate data has nested braces but no control-flow complexity.
void data() {
    constexpr int table[1][1][1][1] = {{{{7}}}};
    consume(table);
}
