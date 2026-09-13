#include "window.hpp"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QObject>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <chrono>
#include <thread>

#include "needle.hpp"
#include "worker.hpp"

namespace qt_loop {
namespace {

/// Every button does this much work. Same total, three different ways.
constexpr auto kWork = std::chrono::milliseconds{3000};
/// One slice. Below one 60 Hz frame, so the needle keeps its appointment.
constexpr auto kChunk = std::chrono::milliseconds{10};
constexpr int kChunks = static_cast<int>(kWork / kChunk);

/// Occupies the calling thread. Whether it sleeps or spins makes no difference
/// to the loop -- all the loop knows is that it did not get control back. A
/// blocking read() on a CAN socket looks identical from here.
void occupy(std::chrono::milliseconds duration) { std::this_thread::sleep_for(duration); }

}  // namespace

Window::Window(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("week 11 -- one thread, one loop, one queue"));

    needle_ = new Needle(this);
    readout_ = new QLabel(this);
    status_ = new QLabel(this);
    progress_ = new QProgressBar(this);
    clicks_ = new QPushButton(this);
    block_ = new QPushButton(QStringLiteral("1. Block 3 s  (the bug)"), this);
    chunk_ = new QPushButton(QStringLiteral("2. Chunk it  (10 ms slices)"), this);
    offload_ = new QPushButton(QStringLiteral("3. Worker thread"), this);
    auto* reset = new QPushButton(QStringLiteral("Reset measurement"), this);

    // Fixed width, so the digits stop jumping sideways as they change.
    readout_->setFont(QFont{QStringLiteral("monospace")});
    status_->setWordWrap(true);

    auto* work_row = new QHBoxLayout;
    work_row->addWidget(block_);
    work_row->addWidget(chunk_);
    work_row->addWidget(offload_);

    auto* footer = new QHBoxLayout;
    footer->addWidget(reset);
    footer->addWidget(clicks_);

    auto* root = new QVBoxLayout(this);
    root->addWidget(needle_, 0, Qt::AlignHCenter);
    root->addWidget(readout_);
    root->addWidget(progress_);
    root->addWidget(status_);
    root->addLayout(work_row);
    root->addLayout(footer);

    connect(block_, &QPushButton::clicked, this, &Window::block_the_loop);
    connect(chunk_, &QPushButton::clicked, this, &Window::start_chunked);
    connect(offload_, &QPushButton::clicked, this, &Window::start_worker);
    connect(reset, &QPushButton::clicked, this, [this] {
        monitor_.reset();
        say(QStringLiteral("measuring from here"));
    });

    // Not a decoration. Click this during the freeze: the count does not move,
    // and then jumps by everything you pressed. The clicks were never lost --
    // they were queued, which is what "the UI hung" actually means.
    connect(clicks_, &QPushButton::clicked, this, [this] {
        ++click_count_;
        clicks_->setText(QStringLiteral("Click me  --  %1").arg(click_count_));
    });
    clicks_->setText(QStringLiteral("Click me  --  0"));

    // The needle and the readout are driven by the loop, not by the work. If the
    // loop stops, they stop. That is the demo, and it needed no extra code.
    needle_timer_.setInterval(static_cast<int>(monitor_.interval().count()));
    connect(&needle_timer_, &QTimer::timeout, this, [this] {
        needle_->advance();
        refresh_readout();
    });
    needle_timer_.start();

    // Interval 0 means "next time round the loop, once nothing else is waiting".
    chunk_timer_.setInterval(0);
    connect(&chunk_timer_, &QTimer::timeout, this, &Window::run_one_chunk);

    connect(&monitor_, &av::qt::LoopMonitor::stalled, this, [this](int late_ms) {
        say(QStringLiteral("the loop was blocked for %1 ms -- every repaint, timer "
                           "and click waited that long")
                .arg(late_ms));
    });
    monitor_.start();

    // Connected now, while both objects are still here, and moved afterwards.
    // The connection survives the move, and Qt::AutoConnection re-decides
    // direct-or-queued at every emit from the affinities it finds then.
    connect(this, &Window::work_requested, &worker_, &Worker::run);
    connect(&worker_, &Worker::progressed, progress_, &QProgressBar::setValue);
    connect(&worker_, &Worker::finished, this, [this] {
        say(QStringLiteral("done. The worker blocked for 3 s and nobody minded: that "
                           "thread has no UI to starve. Note that no mutex was "
                           "written -- the queued connection is the synchronisation"));
        set_busy(false);
    });
    worker_.moveToThread(&worker_thread_);
    // Started now rather than on first use: an idle QThread costs a stack and
    // nothing else, and it keeps start_worker() down to one meaningful line.
    worker_thread_.start();

    say(QStringLiteral("watch the needle, then press 1, 2 and 3 in turn and read `worst`"));
}

Window::~Window() {
    // Ask, then wait. quit() ends the thread's event loop but cannot interrupt a
    // slot already running inside it -- which is why Worker has a cancel() flag,
    // and why without it wait() would sit here for up to three seconds. Week 2's
    // ordered shutdown, with Qt's names on it.
    worker_.cancel();
    worker_thread_.quit();
    worker_thread_.wait();
}

void Window::block_the_loop() {
    set_busy(true);
    progress_->setValue(0);
    say(QStringLiteral("blocking this thread for 3 s. Click 'Click me' while it is "
                       "frozen and watch where those clicks go"));

    // The message has to reach the screen *before* the screen stops updating, so
    // the blocking goes on the next trip round the loop rather than in this slot.
    // A zero-interval singleShot is how you say "after I return". The
    // alternative -- QCoreApplication::processEvents() right here -- is the
    // habit this entire week exists to argue against.
    QTimer::singleShot(0, this, [this] {
        occupy(kWork);
        progress_->setValue(100);
        say(QStringLiteral("back. Read `worst` above: that is how long a driver "
                           "stared at a cluster that had stopped being a cluster"));
        set_busy(false);
    });
}

void Window::start_chunked() {
    set_busy(true);
    chunks_left_ = kChunks;
    progress_->setValue(0);
    say(QStringLiteral("the same 3 s of work, in %1 ms slices -- the loop gets "
                       "control back between every one")
            .arg(static_cast<long long>(kChunk.count())));
    chunk_timer_.start();
}

void Window::run_one_chunk() {
    occupy(kChunk);
    --chunks_left_;
    progress_->setValue((kChunks - chunks_left_) * 100 / kChunks);
    if (chunks_left_ > 0) {
        return;
    }

    chunk_timer_.stop();
    say(QStringLiteral("done, and `worst` stayed in tens of milliseconds instead of "
                       "thousands. Same work, same thread, no extra core -- only the "
                       "size of each bite changed"));
    set_busy(false);
}

void Window::start_worker() {
    set_busy(true);
    progress_->setValue(0);
    say(QStringLiteral("the same 3 s of work, on another thread -- this loop stays "
                       "free the whole time"));
    emit work_requested(static_cast<int>(kWork.count()));
}

void Window::set_busy(bool busy) {
    // Only the three work buttons. 'Click me' stays live on purpose: it is the
    // evidence that input is queued rather than dropped.
    block_->setEnabled(!busy);
    chunk_->setEnabled(!busy);
    offload_->setEnabled(!busy);
}

void Window::say(const QString& text) { status_->setText(text); }

void Window::refresh_readout() {
    const auto latency = monitor_.latency();
    readout_->setText(
        QStringLiteral("loop latency   ticks %1   p50 %2 ms   p99 %3 ms   worst %4 ms")
            .arg(static_cast<long long>(latency.ticks), 6)
            .arg(static_cast<long long>(latency.p50.count()), 4)
            .arg(static_cast<long long>(latency.p99.count()), 4)
            .arg(static_cast<long long>(latency.worst.count()), 5));
}

}  // namespace qt_loop
