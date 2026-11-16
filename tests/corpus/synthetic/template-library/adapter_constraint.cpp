// Synthetic library: constructing an insert iterator does not instantiate its assignment body.
template<class Container> concept AdapterOnly = requires(Container c) {
    std::inserter(c, c.end());
};
