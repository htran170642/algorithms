// W1 — FixedString: the null terminator is the feature.
//
// The tests all circle one property: no matter what you do to the object, c_str()
// hands a C API a properly terminated string. That is the reason this type exists
// instead of FixedVector<char, N>.

#include "av/fixed_string.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string_view>

// --- the headline claim: always null-terminated --------------------------------

TEST(FixedString, IsNullTerminatedEvenWhenEmpty) {
  const av::FixedString<8> s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0U);
  // A C API called on a fresh object must see an empty string, not garbage.
  EXPECT_STREQ(s.c_str(), "");
  EXPECT_EQ(std::strlen(s.c_str()), 0U);
}

TEST(FixedString, StaysNullTerminatedAfterAppend) {
  av::FixedString<8> s;
  ASSERT_TRUE(s.try_append("CAN"));
  EXPECT_STREQ(s.c_str(), "CAN");

  // strlen walks until it finds '\0'. If the terminator were missing it would run
  // off the end of the buffer — the exact bug this class prevents.
  EXPECT_EQ(std::strlen(s.c_str()), s.size());
}

TEST(FixedString, StaysNullTerminatedWhenCompletelyFull) {
  av::FixedString<3> s;
  ASSERT_TRUE(s.try_append("ABC"));
  EXPECT_TRUE(s.full());

  // The dangerous case: every declared character is used. The terminator lives in
  // the extra +1 byte, so it is still there.
  EXPECT_STREQ(s.c_str(), "ABC");
  EXPECT_EQ(std::strlen(s.c_str()), 3U);
}

TEST(FixedString, StorageIsCapacityPlusOne) {
  // Capacity characters + 1 terminator, and no heap pointer anywhere.
  EXPECT_GE(sizeof(av::FixedString<8>), 8U + 1U);
}

// --- the two overflow policies -------------------------------------------------

TEST(FixedString, TryAppendIsAllOrNothing) {
  av::FixedString<8> s;
  ASSERT_TRUE(s.try_append("ECU"));

  // Does not fit -> nothing is written at all. Use this where a shortened value
  // would be WRONG: a path, a DTC code, an identifier.
  EXPECT_FALSE(s.try_append("0123456789"));
  EXPECT_STREQ(s.c_str(), "ECU") << "a failed try_append must not modify anything";
  EXPECT_EQ(s.size(), 3U);
}

TEST(FixedString, AppendTruncatingReportsWhatItDropped) {
  av::FixedString<5> s;

  // Takes what fits, and RETURNS the number of dropped characters so the caller
  // can notice. Truncation must never be silent.
  const std::size_t dropped = s.append_truncating("VehicleSpeed");
  EXPECT_EQ(s.size(), 5U);
  EXPECT_STREQ(s.c_str(), "Vehic");
  EXPECT_EQ(dropped, std::strlen("VehicleSpeed") - 5U);
}

TEST(FixedString, AppendTruncatingOnFullStringDropsEverything) {
  av::FixedString<3> s;
  ASSERT_TRUE(s.try_append("ABC"));

  EXPECT_EQ(s.append_truncating("XYZ"), 3U);
  EXPECT_STREQ(s.c_str(), "ABC");
}

TEST(FixedString, ConstructorTruncates) {
  const av::FixedString<4> s{std::string_view("CoolantTemp")};
  EXPECT_STREQ(s.c_str(), "Cool");
  EXPECT_EQ(s.size(), 4U);
}

// --- ordinary behaviour ---------------------------------------------------------

TEST(FixedString, ClearResetsToEmptyString) {
  av::FixedString<8> s;
  ASSERT_TRUE(s.try_append("RPM"));
  s.clear();

  EXPECT_TRUE(s.empty());
  EXPECT_STREQ(s.c_str(), "") << "clear must restore the terminator, not just size";
}

TEST(FixedString, AppendsAccumulate) {
  av::FixedString<16> s;
  ASSERT_TRUE(s.try_append("Engine"));
  ASSERT_TRUE(s.try_append("RPM"));
  EXPECT_STREQ(s.c_str(), "EngineRPM");
  EXPECT_EQ(s.view(), std::string_view("EngineRPM"));
}

TEST(FixedString, ComparesByContentAcrossCapacities) {
  // view() is why this works: different capacities are different TYPES, but they
  // meet on the one interface type, std::string_view.
  const av::FixedString<8> a{std::string_view("DTC")};
  const av::FixedString<32> b{std::string_view("DTC")};
  const av::FixedString<8> c{std::string_view("RPM")};

  EXPECT_TRUE(a == b);
  EXPECT_TRUE(a != c);
}
