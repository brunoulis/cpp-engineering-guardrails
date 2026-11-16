// Synthetic ABI facade: forwarding through PImpl is deliberate.
class Facade { public: void run(); private: class Impl; Impl* implementation; };
