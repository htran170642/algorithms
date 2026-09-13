#include "needle.hpp"

#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QSize>
#include <QWidget>

#include <algorithm>

namespace qt_loop {
namespace {

constexpr int kStepDegrees = 6;  // 60 steps per revolution
constexpr int kFullCircle = 360;
constexpr int kMargin = 8;
constexpr int kSide = 130;

}  // namespace

Needle::Needle(QWidget* parent) : QWidget(parent) {}

void Needle::advance() {
    degrees_ = (degrees_ + kStepDegrees) % kFullCircle;

    // update() does not paint. It asks the loop for a paint event, which arrives
    // on some later trip round -- so even the drawing is queued work, and a
    // blocked slot postpones it like everything else. repaint() would paint now,
    // and is the wrong answer for the same reason processEvents() usually is.
    update();
}

QSize Needle::sizeHint() const { return QSize{kSide, kSide}; }

void Needle::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);

    const int radius = (std::min(width(), height()) - 2 * kMargin) / 2;

    // 2.0, not 2: translate() takes qreal, and integer division would throw the
    // half-pixel away before the promotion -- so on any odd width the needle
    // would pivot half a pixel off centre and wobble as it turned. Found by
    // clang-tidy's bugprone-integer-division, not by looking at it.
    painter.translate(width() / 2.0, height() / 2.0);

    painter.setPen(QPen{palette().mid().color(), 2});
    painter.drawEllipse(QPoint{0, 0}, radius, radius);

    painter.rotate(degrees_);
    painter.setPen(QPen{palette().highlight().color(), 5, Qt::SolidLine, Qt::RoundCap});
    painter.drawLine(0, 0, 0, -radius + kMargin);
}

}  // namespace qt_loop
