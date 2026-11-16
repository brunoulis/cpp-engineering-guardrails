// Synthetic engine: owned std::jthread joins at scope exit; no detach call.
void simulate() { std::jthread worker([] { step(); }); }
