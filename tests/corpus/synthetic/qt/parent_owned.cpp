// Synthetic Qt idiom; assumes QObject-derived QLabel and the usual parent contract.
Panel::Panel(QWidget* parent) : QWidget(parent) {
    auto* label = new QLabel("name", this);
    connect(timer, &QTimer::timeout, this, [this] { refresh(); });
}
