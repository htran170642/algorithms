#pragma once

// The heartbeat. Its only job is to be obviously alive -- or obviously not.
//
// A number that stops updating is easy to miss. A needle that stops sweeping is
// not, which is more or less why a cluster has one in the first place.

#include <QSize>
#include <QWidget>

namespace qt_loop {

class Needle : public QWidget {
    Q_OBJECT

public:
    explicit Needle(QWidget* parent = nullptr);

    /// Advance one step. Driven by a QTimer on the UI thread, so a blocked loop
    /// simply does not call it -- which is the entire demo.
    void advance();

    [[nodiscard]] QSize sizeHint() const override;

protected:
    // Qt names its virtuals in camelBack and an override has to match, which is
    // why apps/qt_loop/.clang-tidy switches the naming check off here.
    void paintEvent(QPaintEvent* event) override;

private:
    int degrees_{0};
};

}  // namespace qt_loop
