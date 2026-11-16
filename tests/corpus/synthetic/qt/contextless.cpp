// Synthetic Qt lifetime review candidate: receiver lifetime is not connected.
void Panel::wire() {
    connect(timer, &QTimer::timeout, [this] { refresh(); });
}
