// Synthetic backend: manual ownership loses cleanup when consume throws.
void backend() {
    auto* request = new Request;
    consume(request);
    delete request;
}
