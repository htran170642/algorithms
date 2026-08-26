#include "av/log.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class LogTest : public ::testing::Test {
protected:
    void SetUp() override {
        lines_.clear();
        av::log::set_level(av::log::Level::Trace);
        av::log::set_sink([this](std::string_view line) { lines_.emplace_back(line); });
    }

    void TearDown() override {
        av::log::set_sink({});
        av::log::set_level(av::log::Level::Info);
    }

    std::vector<std::string> lines_;
};

TEST_F(LogTest, WritesLevelComponentAndMessage) {
    av::log::info("can.rx", "frame decoded");

    ASSERT_EQ(lines_.size(), 1U);
    EXPECT_NE(lines_[0].find("INFO"), std::string::npos);
    EXPECT_NE(lines_[0].find("can.rx"), std::string::npos);
    EXPECT_NE(lines_[0].find("frame decoded"), std::string::npos);
}

TEST_F(LogTest, AppendsKeyValuePairs) {
    av::log::info("can.rx", "frame decoded", "id", 291, "dlc", 8);

    ASSERT_EQ(lines_.size(), 1U);
    EXPECT_NE(lines_[0].find(" id=291 dlc=8"), std::string::npos);
}

TEST_F(LogTest, FormatsBoolAsWord) {
    av::log::warn("door.fl", "state changed", "open", true);

    ASSERT_EQ(lines_.size(), 1U);
    EXPECT_NE(lines_[0].find("open=true"), std::string::npos);
}

TEST_F(LogTest, AppendsNothingWhenNoFields) {
    av::log::error("can.bus", "bus off");

    ASSERT_EQ(lines_.size(), 1U);
    EXPECT_EQ(lines_[0].back(), 'f');  // ends with the message, no stray '='
}

TEST_F(LogTest, DropsMessagesBelowConfiguredLevel) {
    av::log::set_level(av::log::Level::Warn);

    av::log::info("can.rx", "chatty");
    av::log::error("can.bus", "bus off");

    ASSERT_EQ(lines_.size(), 1U);
    EXPECT_NE(lines_[0].find("bus off"), std::string::npos);
}

TEST_F(LogTest, StartsWithIsoTimestamp) {
    av::log::info("boot", "up");

    ASSERT_EQ(lines_.size(), 1U);
    const std::string& line = lines_[0];
    ASSERT_GE(line.size(), 23U);
    EXPECT_EQ(line[4], '-');
    EXPECT_EQ(line[7], '-');
    EXPECT_EQ(line[10], 'T');
    EXPECT_EQ(line[13], ':');
    EXPECT_EQ(line[16], ':');
    EXPECT_EQ(line[19], '.');
}

// The cockpit pipeline has a receiver thread, a processing thread and the UI
// thread all logging. Run under the tsan preset to make this test meaningful.
TEST_F(LogTest, ConcurrentWritersProduceIntactLines) {
    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int id = 0; id < kThreads; ++id) {
        workers.emplace_back([id] {
            for (int seq = 0; seq < kPerThread; ++seq) {
                av::log::info("worker", "tick", "thread", id, "seq", seq);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }

    ASSERT_EQ(lines_.size(), static_cast<std::size_t>(kThreads) * kPerThread);
    for (const std::string& line : lines_) {
        EXPECT_NE(line.find(" thread="), std::string::npos);
        EXPECT_NE(line.find(" seq="), std::string::npos);
    }
}

}  // namespace
