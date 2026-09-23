#include <catch2/catch_test_macros.hpp>

#include <ymir/hw/sh2/sh2.hpp>

#include <optional>
#include <vector>

using namespace ymir;

namespace {

struct SerialCapture {
    std::vector<uint8> bytes;

    void Transmit(uint8 value) {
        bytes.push_back(value);
    }
};

struct SerialFeed {
    std::vector<uint8> bytes;
    size_t next = 0;

    std::optional<uint8> Receive() {
        if (next >= bytes.size()) {
            return std::nullopt;
        }
        return bytes[next++];
    }
};

} // namespace

TEST_CASE("SH-2 SCI register defaults and CPU-driven transmission", "[sh2][sci]") {
    sys::SH2Bus bus{};
    sh2::SH2 cpu{bus, true};
    auto &probe = cpu.GetProbe();
    SerialCapture capture{};
    cpu.SetSCITransmitCallback(util::MakeClassMemberOptionalCallback<&SerialCapture::Transmit>(&capture));

    CHECK(probe.MemReadByte(0xFFFF'FE00u, false) == 0x00);
    CHECK(probe.MemReadByte(0xFFFF'FE01u, false) == 0xFF);
    CHECK(probe.MemReadByte(0xFFFF'FE02u, false) == 0x00);
    CHECK(probe.MemReadByte(0xFFFF'FE03u, false) == 0xFF);
    CHECK(probe.MemReadByte(0xFFFF'FE04u, false) == 0x84);

    probe.MemWriteByte(0xFFFF'FE02u, 0x20, false); // TE
    probe.MemWriteByte(0xFFFF'FE03u, 0x42, false); // TDR
    probe.MemWriteByte(0xFFFF'FE04u, 0x7F, false); // clear TDRE after reading SSR

    REQUIRE(capture.bytes.size() == 1);
    CHECK(capture.bytes[0] == 0x42);
    CHECK(probe.MemReadByte(0xFFFF'FE04u, false) == 0x84);
}

TEST_CASE("SH-2 SCI receive flags and interrupts", "[sh2][sci]") {
    sys::SH2Bus bus{};
    sh2::SH2 cpu{bus, true};
    auto &probe = cpu.GetProbe();
    probe.INTC().SetLevel(sh2::InterruptSource::SCI_RXI, 15);
    probe.INTC().SetLevel(sh2::InterruptSource::SCI_ERI, 15);

    probe.MemWriteByte(0xFFFF'FE02u, 0x50, false); // RIE + RE
    REQUIRE(cpu.SCIReceiveByte(0x27));
    CHECK(probe.MemReadByte(0xFFFF'FE05u, false) == 0x27);
    CHECK((probe.MemReadByte(0xFFFF'FE04u, false) & 0x40) != 0);
    CHECK(probe.IsInterruptRaised(sh2::InterruptSource::SCI_RXI));

    CHECK_FALSE(cpu.SCIReceiveByte(0x28)); // overrun before RDRF is cleared
    CHECK((probe.MemReadByte(0xFFFF'FE04u, false) & 0x20) != 0);
    CHECK(probe.IsInterruptRaised(sh2::InterruptSource::SCI_ERI));

    probe.MemWriteByte(0xFFFF'FE04u, 0x9F, false); // clear RDRF and ORER
    CHECK((probe.MemReadByte(0xFFFF'FE04u, false) & 0x60) == 0);
    CHECK_FALSE(probe.IsInterruptRaised(sh2::InterruptSource::SCI_RXI));
    CHECK_FALSE(probe.IsInterruptRaised(sh2::InterruptSource::SCI_ERI));
}

TEST_CASE("SH-2 SCI pulls pending external bytes when the guest polls status", "[sh2][sci]") {
    sys::SH2Bus bus{};
    sh2::SH2 cpu{bus, true};
    auto &probe = cpu.GetProbe();
    SerialFeed feed{{0x31, 0x32}};
    cpu.SetSCIReceiveCallback(util::MakeClassMemberOptionalCallback<&SerialFeed::Receive>(&feed));

    probe.MemWriteByte(0xFFFF'FE02u, 0x10, false); // RE
    CHECK((probe.MemReadByte(0xFFFF'FE04u, false) & 0x40) != 0);
    CHECK(probe.MemReadByte(0xFFFF'FE05u, false) == 0x31);
    CHECK(feed.next == 1);

    probe.MemWriteByte(0xFFFF'FE04u, 0xBF, false); // clear RDRF
    CHECK((probe.MemReadByte(0xFFFF'FE04u, false) & 0x40) != 0);
    CHECK(probe.MemReadByte(0xFFFF'FE05u, false) == 0x32);
    CHECK(feed.next == 2);
}
