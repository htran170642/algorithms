// Week 11. QObject, signals, slots and the event loop -- learned by breaking it.
//
//     ./build/debug/apps/qt_loop/qt_loop
//
// Press 1, 2, 3 in turn and read `worst` after each. All three do exactly three
// seconds of work; only one of them stops the cluster from being a cluster.
//
// The point of the week is not Qt. It is that week 10 already built this shape
// by hand -- av_mw::Runtime owns no thread, poll() runs callbacks on the
// caller's thread, and a slow callback stalls everything. Qt is that same
// contract with a framework's vocabulary:
//
//     av_mw                              Qt
//     while (!stop) runtime->poll()      QApplication::exec()
//     callback must not block            slot must not block
//     runtime->post(fn)                  Qt::QueuedConnection
//     weak_ptr in every handler          QObject destructor disconnects
//     runtime->fd()                      QSocketNotifier          <- week 12

#include <QApplication>

#include "window.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    qt_loop::Window window;
    window.show();

    // exec() *is* the loop. Everything after this line happens because the loop
    // delivered it -- a timer, a click, a repaint, a signal from another thread.
    // There is no `while` here, unlike svc_client's `while (g_stop == 0)`,
    // because Qt wrote that loop already; it returns when the last window
    // closes. Which also means: whatever does not return to exec() stops time.
    return QApplication::exec();
}
