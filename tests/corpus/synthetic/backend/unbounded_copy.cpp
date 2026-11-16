// Synthetic backend: no capacity proof exists for caller-controlled input.
void unsafe_copy(const char* input) { char buffer[4]; strcpy(buffer, input); }
