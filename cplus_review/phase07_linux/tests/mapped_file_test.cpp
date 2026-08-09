#include "mapped_file.hpp"

#include <gtest/gtest.h>

#include <unistd.h>  // mkstemp, write, close, unlink

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

// VmRSS (resident pages, in kB) of this process from /proc/self/status. A robust,
// batching-independent signal: however many faults it takes, touching N bytes of a
// mapping makes ~N bytes resident. Returns 0 if the field can't be read.
long current_rss_kb() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            long kb = 0;
            std::string unit;
            if (iss >> kb >> unit) {
                return kb;
            }
            return 0;
        }
    }
    return 0;
}

// Creates a uniquely-named temp file with the given contents and unlinks it on
// destruction. Fresh writes leave the pages in the page cache, so a subsequent
// mmap fault on them is a MINOR fault (no disk I/O) -- which is what test 2 relies on.
class TempFile {
public:
    explicit TempFile(const std::string& contents) {
        namespace fs = std::filesystem;
        const std::string tmpl = (fs::temp_directory_path() / "cr_w46_XXXXXX").string();
        std::vector<char> mutable_tmpl(tmpl.begin(), tmpl.end());
        mutable_tmpl.push_back('\0');

        const int fd = ::mkstemp(mutable_tmpl.data());
        if (fd == -1) {
            throw std::runtime_error("mkstemp failed");
        }
        path_ = mutable_tmpl.data();

        std::size_t off = 0;
        while (off < contents.size()) {
            const ssize_t n = ::write(fd, contents.data() + off, contents.size() - off);
            if (n <= 0) {
                ::close(fd);
                throw std::runtime_error("write failed");
            }
            off += static_cast<std::size_t>(n);
        }
        ::close(fd);
    }
    ~TempFile() {
        if (!path_.empty()) {
            ::unlink(path_.c_str());
        }
    }
    TempFile(const TempFile&)            = delete;
    TempFile& operator=(const TempFile&) = delete;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

private:
    std::string path_;
};

}  // namespace

// mmap gives a zero-copy view; the bytes read back must equal what we wrote.
TEST(W46VirtualMemory, MapsFileAndReadsSameBytesAsFile) {
    const std::string contents = "the quick brown fox\n";
    const TempFile tf(contents);

    const cr::MappedFile mf = cr::MappedFile::open_readonly(tf.path());
    ASSERT_EQ(mf.size(), contents.size());
    ASSERT_NE(mf.data(), nullptr);
    EXPECT_EQ(std::string(mf.data(), mf.size()), contents);
}

// Touching each page of a freshly-written (page-cached) file must generate
// MINOR page faults -- the observable proof of demand paging via mmap.
TEST(W46VirtualMemory, TouchingMappedPagesTriggersMinorFaults) {
    constexpr std::size_t kBytes = 32UL * 1024 * 1024;
    const TempFile tf(std::string(kBytes, '\x5A'));

    const cr::MappedFile mf = cr::MappedFile::open_readonly(tf.path());
    ASSERT_EQ(mf.size(), kBytes);

    const long           rss_before   = current_rss_kb();
    const cr::PageFaults fault_before = cr::read_page_faults();
    ASSERT_GT(rss_before, 0);

    // Touch one byte per 4 KiB page; volatile sink defeats dead-code elimination.
    volatile unsigned long sink = 0;
    const std::byte* p = mf.bytes().data();
    for (std::size_t i = 0; i < mf.size(); i += 4096) {
        sink += std::to_integer<unsigned long>(p[i]);
    }
    const long           rss_after   = current_rss_kb();
    const cr::PageFaults fault_after = cr::read_page_faults();
    (void)sink;

    // Robust signal: the file's pages became resident on demand, so RSS grew by
    // ~the file size (allow slack for batching / eviction).
    EXPECT_GT(rss_after - rss_before, static_cast<long>(kBytes / 1024) / 2);

    // The faults are MINOR: pages were already page-cache-hot (we just wrote the
    // file), so no disk I/O => major stays 0. The exact minor count is
    // kernel-dependent (fault-around / large folios batch many pages per fault),
    // so we only assert it is positive, not its magnitude.
    EXPECT_GT(fault_after.minor - fault_before.minor, 0);
    EXPECT_EQ(fault_after.major - fault_before.major, 0);
}

// An empty file has nothing to map: EINVAL is avoided by not calling mmap.
TEST(W46VirtualMemory, EmptyFileMapsToEmptyRange) {
    const TempFile tf("");

    const cr::MappedFile mf = cr::MappedFile::open_readonly(tf.path());
    EXPECT_TRUE(mf.empty());
    EXPECT_EQ(mf.size(), 0u);
    EXPECT_EQ(mf.data(), nullptr);
    EXPECT_TRUE(mf.bytes().empty());
}

// Move-only ownership: the moved-from object is emptied so only one munmap runs
// (ASAN would flag a double-munmap otherwise).
TEST(W46VirtualMemory, MoveTransfersOwnership) {
    const std::string contents = "ownership";
    const TempFile tf(contents);

    cr::MappedFile a = cr::MappedFile::open_readonly(tf.path());
    const char* addr = a.data();
    ASSERT_NE(addr, nullptr);

    const cr::MappedFile b = std::move(a);
    EXPECT_TRUE(a.empty());          // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(a.data(), nullptr);    // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(b.size(), contents.size());
    EXPECT_EQ(b.data(), addr);       // same mapping handed over, not re-mapped
    EXPECT_EQ(std::string(b.data(), b.size()), contents);
}
