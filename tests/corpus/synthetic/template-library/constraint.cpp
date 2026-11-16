// Synthetic library: well-formed requires expression plus a concrete same-type operation.
template<class T> requires requires(T value) { value + value; }
T twice(T value) { return value + value; }
