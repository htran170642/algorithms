#pragma once

// Week 11's demo: the whole week in three buttons.
//
// Each of the three does exactly three seconds of work. Only *how* differs, and
// the latency readout is how you tell them apart:
//
//   1. Block 3 s       one slot, three seconds        worst ~3000 ms
//   2. Chunk it        the same work in 10 ms slices  worst ~tens of ms
//   3. Worker thread   the same work, elsewhere       worst ~tens of ms
//
// Nothing here is Qt-specific in spirit. It is week 10's rule -- a callback must
// return so poll() can keep going -- with a window attached so it can be seen.

#include <QString>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <chrono>

#include "av/qt/loop_monitor.hpp"
#include "worker.hpp"

class QLabel;
class QProgressBar;
class QPushButton;

namespace qt_loop {

class Needle;

class Window : public QWidget {
    Q_OBJECT

public:
    explicit Window(QWidget* parent = nullptr);
    ~Window() override;

    // Week 1's Rule of 5, in Qt's spelling. A user-declared destructor
    // suppresses the move operations but not the *question*, and a widget must
    // not be copied or moved: its identity is its place in a parent's child
    // list, and two of it would be deleted twice.
    Q_DISABLE_COPY_MOVE(Window)

signals:
    /// Asks the worker to start. A signal rather than a direct call, because the
    /// worker lives on another thread -- so Qt turns this into a posted event
    /// and the call lands on the right side of the boundary by itself.
    void work_requested(int total_ms);

private:
    void block_the_loop();
    void start_chunked();
    void run_one_chunk();
    void start_worker();

    void set_busy(bool busy);
    void say(const QString& text);
    void refresh_readout();

    // 16 ms: one frame at 60 Hz, which is the budget a cluster actually has.
    av::qt::LoopMonitor monitor_{std::chrono::milliseconds{16}, this};
    QTimer needle_timer_{this};
    QTimer chunk_timer_{this};
    int chunks_left_{0};
    int click_count_{0};

    // Declared before `worker_` so that it is destroyed after it: an object
    // whose affinity points at a QThread must not outlive that QThread. Week
    // 10's "declaration order matters" row, met again in another framework.
    QThread worker_thread_;
    Worker worker_;

    // Raw, parented pointers -- Qt's own ownership model, in which the parent's
    // destructor is the release. See apps/qt_loop/.clang-tidy.
    Needle* needle_{nullptr};
    QLabel* readout_{nullptr};
    QLabel* status_{nullptr};
    QProgressBar* progress_{nullptr};
    QPushButton* clicks_{nullptr};
    QPushButton* block_{nullptr};
    QPushButton* chunk_{nullptr};
    QPushButton* offload_{nullptr};
};

}  // namespace qt_loop
