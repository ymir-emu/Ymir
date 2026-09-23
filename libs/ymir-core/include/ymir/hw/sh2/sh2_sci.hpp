#pragma once

#include <ymir/core/types.hpp>

namespace ymir::sh2 {

// addr r/w  access   init      code    name
// 000  R/W  8        00        SMR     Serial Mode Register
//
//   b  r/w  code  description
//   7  R/W  C/nA  Communication Mode (0=async, 1=clocked sync)
//   6  R/W  CHR   Character Length (0=8-bit, 1=7-bit)
//   5  R/W  PE    Parity Enable (0=disable, 1=enable)
//   4  R/W  O/nE  Parity Mode (0=even, 1=odd)
//   3  R/W  STOP  Stop Bit Length (0=one, 1=two)
//   2  R/W  MP    Multiprocessor Mode (0=disabled, 1=enabled)
//   1  R/W  CKS1  Clock Select bit 1  (00=phi/4,  01=phi/16,
//   0  R/W  CKS0  Clock Select bit 0   10=phi/64, 11=phi/256)

// 001  R/W  8        FF        BRR     Bit Rate Register

// 002  R/W  8        00        SCR     Serial Control Register

// 003  R/W  8        FF        TDR     Transmit Data Register

// 004  R/W* 8        84        SSR     Serial Status Register
//   * Can only write a 0 to clear the flags

// 005  R    8        00        RDR     Receive Data Register

} // namespace ymir::sh2

namespace ymir::sh2 {

/// @brief SH7604 serial communication interface register state.
///
/// The external serial connection is deliberately kept outside the SH-2 core.
/// The owner delivers received bytes on the emulation thread and receives
/// transmitted bytes through a callback.
struct SerialCommunicationInterface {
    static constexpr uint8 kTDRE = 0x80;
    static constexpr uint8 kRDRF = 0x40;
    static constexpr uint8 kORER = 0x20;
    static constexpr uint8 kTEND = 0x04;
    static constexpr uint8 kTIE = 0x80;
    static constexpr uint8 kRIE = 0x40;
    static constexpr uint8 kTE = 0x20;
    static constexpr uint8 kRE = 0x10;
    static constexpr uint8 kTEIE = 0x04;

    uint8 SMR = 0x00;
    uint8 BRR = 0xFF;
    uint8 SCR = 0x00;
    uint8 TDR = 0xFF;
    uint8 SSR = kTDRE | kTEND;
    uint8 RDR = 0x00;

    // Flags may only be cleared after the guest has observed them in SSR.
    uint8 observedStatus = 0;

    void Reset() {
        *this = {};
    }

    template <bool peek>
    uint8 ReadSSR() {
        if constexpr (!peek) {
            observedStatus |= SSR;
        }
        return SSR;
    }

    /// @return true if clearing TDRE started a CPU-driven transmission.
    template <bool poke>
    bool WriteSSR(uint8 value) {
        if constexpr (poke) {
            SSR = (value & ~kTEND) | (SSR & kTEND);
            observedStatus = 0;
            return false;
        } else {
            const uint8 clear = SSR & observedStatus & ~value & 0xF8;
            SSR &= ~clear;
            observedStatus &= ~clear;
            const bool transmit = (clear & kTDRE) && (SCR & kTE);
            if (transmit) {
                SSR &= ~kTEND;
            }
            return transmit;
        }
    }

    void WriteSCR(uint8 value) {
        SCR = value;
        if (!(SCR & kTE)) {
            SSR |= kTDRE | kTEND;
        }
    }

    void CompleteTransmit() {
        SSR |= kTDRE | kTEND;
    }

    bool ReceiveByte(uint8 value) {
        if (!(SCR & kRE)) {
            return false;
        }
        if (SSR & kRDRF) {
            SSR |= kORER;
            return false;
        }
        RDR = value;
        SSR |= kRDRF;
        return true;
    }

    bool CanDMATransmit() const {
        return (SCR & kTE) && (SSR & kTDRE);
    }

    bool CanDMAReceive() const {
        return (SCR & kRE) && (SSR & kRDRF);
    }
};

} // namespace ymir::sh2
