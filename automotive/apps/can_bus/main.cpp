// The bus layer, made visible.
//
// Week 1 took a payload apart. This demo goes below the payload, to the two
// mechanisms that decide *whether a frame is ever sent at all*:
//
//     arbitration        who gets the wire when several ECUs want it
//     fault confinement  how a broken ECU is removed without taking the bus
//
// Neither is visible from a SocketCAN read(). Both explain failures that look,
// from the cluster's side, like a signal that simply stopped arriving.

#include <cstddef>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "av/can/arbitration.hpp"
#include "av/can/error_state.hpp"
#include "av/log.hpp"

namespace {

using av::can::arbitrate;
using av::can::arbitration_field;
using av::can::BitValue;
using av::can::Contender;
using av::can::ErrorCounters;
using av::can::ErrorEvent;
using av::can::ErrorState;
using av::can::kRecoveryWindows;
using av::can::to_string;

namespace log = av::log;

/// Column widths for the trace table.
constexpr std::size_t kNodeColumn = 9;
constexpr std::size_t kIndexColumn = 7;
constexpr std::size_t kNameColumn = 10;

/// Flushed, because av::log writes to stderr and this writes to stdout; without
/// it the two streams interleave in whatever order their buffers drain.
void heading(const char* text) { std::cout << '\n' << text << '\n' << std::flush; }

void say(const std::string& line) { std::cout << line << '\n' << std::flush; }

std::string pad(std::string_view text, std::size_t width) {
    std::string out(text);
    if (out.size() < width) {
        out.append(width - out.size(), ' ');
    }
    return out;
}

/// 0 is dominant, 1 is recessive -- the logical bit, which is what a scope
/// trace and a datasheet both use.
std::string bit_cell(BitValue value) { return value == BitValue::Dominant ? "0" : "1"; }

std::string table_header(const std::vector<Contender>& bus) {
    std::string header = pad("  bit", kIndexColumn) + pad("name", kNameColumn);
    for (const auto& node : bus) {
        header += pad(node.name, kNodeColumn);
    }
    return header + "bus";
}

/// Runs one round and prints it bit by bit, which is the only way the mechanism
/// becomes obvious: a loser stops driving the instant it reads a dominant bit
/// it did not send, so it never puts a wrong bit on the wire.
///
/// Returns the number of winners: 1 normally, 0 if the bus could not be
/// simulated at all, more than 1 when two nodes drove identical fields.
std::size_t show_round(const std::vector<Contender>& bus) {
    const auto result = arbitrate(bus);
    if (!result) {
        log::error("can.arb", "bus cannot be simulated", "nodes", bus.size());
        return 0U;
    }

    std::vector<std::vector<BitValue>> fields;
    fields.reserve(bus.size());
    for (const auto& node : bus) {
        fields.push_back(arbitration_field(node));
    }

    say(table_header(bus));

    std::vector<bool> withdrawn(bus.size(), false);
    for (const auto& step : result->trace) {
        std::string line = pad("  " + std::to_string(step.index), kIndexColumn);
        line += pad(step.name, kNameColumn);
        for (std::size_t node = 0; node < bus.size(); ++node) {
            const std::string cell =
                withdrawn[node] ? std::string{"."} : bit_cell(fields[node][step.index]);
            line += pad(cell, kNodeColumn);
        }
        line += bit_cell(step.bus);

        for (const std::size_t node : step.lost) {
            line += "   <- " + std::string(bus[node].name) + " sent 1, read 0, withdraws";
            withdrawn[node] = true;
        }
        say(line);
    }

    if (result->winners.size() == 1U) {
        log::info("can.arb", "bus granted", "winner", bus[result->winners.front()].name, "bits",
                  result->trace.size());
    } else {
        log::error("can.arb", "arbitration cannot resolve this", "still_transmitting",
                   result->winners.size());
        for (const std::size_t node : result->winners) {
            log::error("can.arb", "identical arbitration field", "node", bus[node].name, "id",
                       bus[node].id);
        }
    }
    return result->winners.size();
}

/// Drives one node from healthy to bus-off and back, logging only transitions --
/// which is exactly what a diagnostic trace would show.
void show_fault_confinement() {
    ErrorCounters node;
    ErrorState last = node.state();
    log::info("can.err", "controller powered on", "tec", node.transmit_errors(), "state",
              to_string(last));

    for (unsigned attempt = 1; attempt <= 40U && node.state() != ErrorState::BusOff; ++attempt) {
        node.on(ErrorEvent::TransmitError);
        if (node.state() == last) {
            continue;
        }
        last = node.state();
        log::warn("can.err", "fault confinement state change", "failed_frames", attempt, "tec",
                  node.transmit_errors(), "state", to_string(last));
    }

    say("");
    say("   Error passive at 16 failures, bus-off at 32. Note what did NOT happen");
    say("   at 16: the node kept transmitting normally. Error passive costs it the");
    say("   right to signal errors and 8 bits of delay before each frame -- not");
    say("   its ability to talk. No application layer sees that.");
    say("");

    for (unsigned window = 0; window < kRecoveryWindows; ++window) {
        node.on(ErrorEvent::RecessiveWindow);
    }
    log::info("can.err", "recovered after a quiet bus", "windows", kRecoveryWindows, "tec",
              node.transmit_errors(), "state", to_string(node.state()));
}

}  // namespace

int main() {
    log::set_level(log::Level::Debug);

    // --- priority ------------------------------------------------------------
    heading("1. Four ECUs want an idle bus at the same instant");
    say("   dominant = 0 (actively driven), recessive = 1 (floating).");
    say("   The bus is wired-AND: one node driving 0 pulls the whole wire to 0.");
    say("");

    const std::vector<Contender> bus{
        {"BRAKE", 0x080U, false, false},
        {"ENGINE", 0x100U, false, false},
        {"BODY", 0x200U, false, false},
        {"INFO", 0x600U, false, false},
    };
    if (show_round(bus) != 1U) {
        return 1;
    }
    say("");
    say("   Nobody collided and nobody retried. The three losers withdrew before");
    say("   sending a single wrong bit -- that is what 'non-destructive' means,");
    say("   and it is why CAN needs no equivalent of Ethernet's backoff.");

    // --- the two tie-breakers ------------------------------------------------
    heading("2. Same identifier: a data frame beats a remote frame");
    if (show_round({{"ASK", 0x123U, false, true}, {"ANSWER", 0x123U, false, false}}) != 1U) {
        return 1;
    }
    say("   Decided on RTR: supplying the value outranks asking for it.");

    heading("3. Same base identifier: a standard frame beats an extended frame");
    if (show_round({{"EXT", 0x123U << 18U, true, false}, {"STD", 0x123U, false, false}}) != 1U) {
        return 1;
    }
    say("   Decided at position 11, where the standard frame sends RTR (dominant)");
    say("   and the extended frame must send SRR (always recessive).");

    // --- the fault arbitration cannot fix ------------------------------------
    heading("4. Fault: two ECUs configured with the same identifier");
    if (show_round({{"ECU-A", 0x123U, false, false}, {"ECU-B", 0x123U, false, false}}) != 2U) {
        return 1;
    }
    say("   Both survive all 13 bits, both keep transmitting, and they destroy");
    say("   each other in the data field. On a real bus this appears as sporadic");
    say("   error frames -- never as a priority complaint.");

    // --- fault confinement ---------------------------------------------------
    heading("5. Fault: one ECU with a failing transceiver");
    show_fault_confinement();

    // --- the week's silent fault ---------------------------------------------
    heading("6. The silent fault: BRAKE is gone and the bus is perfect");
    if (show_round({{"ENGINE", 0x100U, false, false},
                    {"BODY", 0x200U, false, false},
                    {"INFO", 0x600U, false, false}}) != 1U) {
        return 1;
    }
    log::warn("can.rx", "bus healthy, one signal missing", "node", "BRAKE", "cause",
              "bus-off, self-removed");
    say("");
    say("   Nothing here is broken. Load is lower, latency is better, every other");
    say("   signal decodes. The cluster sees exactly one value stop updating, and");
    say("   the evidence is in that controller's error counters -- not in the bus");
    say("   traffic, not in the DBC, and not in the UI.");

    std::cout << '\n' << std::flush;
    return 0;
}
