#pragma once

// The third cure: move the blocking work off the UI thread entirely.
//
// This object is constructed on the UI thread and then moved to a QThread, so
// every slot below runs over there. It reports back with signals, and because
// sender and receiver then live on different threads Qt posts those instead of
// calling them -- which is precisely why nothing here needs a mutex.

#include <QObject>

#include <atomic>

namespace qt_loop {

class Worker : public QObject {
    Q_OBJECT

public:
    /// Callable from any thread, which is exactly why it is an atomic flag and
    /// not a slot. A slot would be queued *behind* the very work we want to
    /// interrupt, and would therefore run only after it finished.
    void cancel() { cancelled_.store(true); }

public slots:
    /// Occupies this thread for `total_ms`, reporting as it goes.
    ///
    /// Blocking here is not the bug it was in Window::block_the_loop. This
    /// thread paints nothing and answers no clicks, so there is nothing to
    /// starve. "Do not block" was never a rule about sleeping -- it is a rule
    /// about the thread that owns the UI.
    void run(int total_ms);

signals:
    void progressed(int percent);
    void finished();

private:
    std::atomic<bool> cancelled_{false};
};

}  // namespace qt_loop
