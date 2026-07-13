#pragma once

// Probe — a type that narrates its own lifetime.
//
// Every construction, copy, move and destruction is counted and (optionally)
// printed. This is how you stop *believing* things about C++ and start
// *measuring* them: "does this line copy?" is a question with an answer.
//
// Reused all year. It comes back in W43 to prove what RVO/NRVO really do.

#include <iostream>
#include <string>
#include <utility>

namespace cr {

struct ProbeCounters {
    int default_ctor = 0;
    int value_ctor   = 0;
    int copy_ctor    = 0;
    int move_ctor    = 0;
    int copy_assign  = 0;
    int move_assign  = 0;
    int dtor         = 0;

    [[nodiscard]] int copies() const noexcept { return copy_ctor + copy_assign; }
    [[nodiscard]] int moves()  const noexcept { return move_ctor + move_assign; }
};

class Probe {
public:
    Probe() : name_("anon") {
        ++counters_.default_ctor;
        log("Probe()");
    }

    explicit Probe(std::string name) : name_(std::move(name)) {
        ++counters_.value_ctor;
        log("Probe(string)");
    }

    Probe(const Probe& other) : name_(other.name_) {
        ++counters_.copy_ctor;
        log("Probe(const Probe&)      <== COPY");
    }

    // noexcept is not decoration. Without it, std::vector will COPY instead of
    // move when it reallocates -- see move_if_noexcept in W5.
    Probe(Probe&& other) noexcept : name_(std::move(other.name_)) {
        other.name_ = "<moved-from>";
        ++counters_.move_ctor;
        log("Probe(Probe&&)           <-- move");
    }

    Probe& operator=(const Probe& other) {
        if (this != &other) {
            name_ = other.name_;
        }
        ++counters_.copy_assign;
        log("operator=(const Probe&)  <== COPY");
        return *this;
    }

    Probe& operator=(Probe&& other) noexcept {
        if (this != &other) {
            name_       = std::move(other.name_);
            other.name_ = "<moved-from>";
        }
        ++counters_.move_assign;
        log("operator=(Probe&&)       <-- move");
        return *this;
    }

    ~Probe() {
        ++counters_.dtor;
        log("~Probe()");
    }

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    static void reset() noexcept { counters_ = ProbeCounters{}; }
    static void setVerbose(bool on) noexcept { verbose_ = on; }

    [[nodiscard]] static const ProbeCounters& counters() noexcept { return counters_; }

private:
    void log(const char* event) const {
        if (verbose_) {
            std::cout << "  " << event << "  [" << name_ << "]\n";
        }
    }

    std::string name_;

    inline static ProbeCounters counters_{};
    inline static bool          verbose_ = true;
};

}  // namespace cr
