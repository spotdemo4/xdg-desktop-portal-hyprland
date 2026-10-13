#include "../src/shared/ClipboardTransfers.hpp"
#include <array>
#include <cassert>
#include <fcntl.h>
#include <unistd.h>

using Hyprutils::OS::CFileDescriptor;

static std::array<CFileDescriptor, 2> makePipe() {
    std::array<int, 2> fds = {-1, -1};
    assert(pipe2(fds.data(), O_CLOEXEC) == 0);
    return {CFileDescriptor{fds[0]}, CFileDescriptor{fds[1]}};
}

// whether the write end of the pipe is closed everywhere, as the reader sees the end of the content
static bool readerSeesEnd(const CFileDescriptor& readFd) {
    assert(fcntl(readFd.get(), F_SETFL, O_NONBLOCK) == 0);
    char c = 0;
    return read(readFd.get(), &c, 1) == 0;
}

int main() {
    CClipboardTransfers transfers;
    assert(transfers.size() == 0);
    assert(!transfers.take(1));

    // each transfer gets its own nonzero serial
    auto [read1, write1] = makePipe();
    auto [read2, write2] = makePipe();
    const auto SERIAL1   = transfers.add(std::move(write1));
    const auto SERIAL2   = transfers.add(std::move(write2));
    assert(SERIAL1 != 0 && SERIAL2 != 0 && SERIAL1 != SERIAL2);
    assert(transfers.size() == 2);

    // the transfer holds the write end open until it's taken
    assert(!readerSeesEnd(read1));

    // taking hands over the fd, once
    auto taken = transfers.take(SERIAL1);
    assert(taken && taken->isValid());
    assert(!transfers.take(SERIAL1));
    assert(transfers.size() == 1);
    assert(write(taken->get(), "x", 1) == 1);
    taken->reset();
    char c = 0;
    assert(read(read1.get(), &c, 1) == 1 && c == 'x');
    assert(readerSeesEnd(read1));

    // a transfer that's dropped ends empty
    transfers.take(SERIAL2);
    assert(transfers.size() == 0);
    assert(readerSeesEnd(read2));

    // and one still pending when the transfers go ends empty too
    auto [read3, write3] = makePipe();
    {
        CClipboardTransfers scoped;
        scoped.add(std::move(write3));
        assert(!readerSeesEnd(read3));
    }
    assert(readerSeesEnd(read3));

    return 0;
}
