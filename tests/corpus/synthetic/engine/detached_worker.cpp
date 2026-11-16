// Synthetic engine: shutdown and referenced-state lifetime need review.
void simulate(State& state) { std::thread([&state] { step(state); }).detach(); }
