#pragma once

// Test-only QObjects.
//
// They live in a header rather than in qt_test.cpp because of how moc is
// driven. A Q_OBJECT written directly in a .cpp works only if that file ends
// with `#include "qt_test.moc"` by hand; a Q_OBJECT in a header needs no such
// line -- but the header must be named in the target's SOURCES, or AUTOMOC
// never looks at it. See the CMakeLists next door: both halves of that rule
// cost this week a link error before they were understood.

#include <QObject>
#include <QThread>

namespace av::qt::test {

/// Emits on demand. The sender half of every connection test.
class Sender : public QObject {
    Q_OBJECT

public:
    void fire(int value) { emit pinged(value); }

signals:
    void pinged(int value);
};

/// Records what it was told, and which thread told it.
class Receiver : public QObject {
    Q_OBJECT

public:
    int calls{0};
    int last{0};
    const QThread* seen_on{nullptr};

public slots:
    void on_pinged(int value) {
        ++calls;
        last = value;
        seen_on = QThread::currentThread();
    }
};

/// Does its work on whatever thread it was moved to, then says so.
///
/// `ran_on` is written on the worker thread and read on the main thread only
/// after QThread::wait(), which is a join and therefore a synchronisation
/// point. No mutex, and none needed -- that is the claim the test checks.
class Worker : public QObject {
    Q_OBJECT

public:
    const QThread* ran_on{nullptr};

public slots:
    void run() {
        ran_on = QThread::currentThread();
        emit done(42);
    }

signals:
    void done(int value);
};

}  // namespace av::qt::test
