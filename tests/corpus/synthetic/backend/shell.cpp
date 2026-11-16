// Synthetic backend: untrusted input reaches shell execution.
void run(const char* user_command) { system(user_command); }
