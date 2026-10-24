#include "sh2_tracer.hpp"

using namespace ymir;

namespace app {

void SH2Tracer::ResetInterruptCounter() {
    m_interruptCounter = 0;
}

void SH2Tracer::ResetDivisionCounter() {
    m_divisionCounter = 0;
}

void SH2Tracer::ResetDMACounter(uint32 channel) {
    m_dmaCounter[channel] = 0;
}

void SH2Tracer::SetInstructionTraceEnabled(bool enable) {
    traceInstructions = enable;
    if (enable && !instructions) {
        instructions = std::make_unique<InstructionBuffer>();
        totalInstructions = 0;
    }
}

SH2Tracer::InstructionTraceInfo SH2Tracer::GetInstructionTraceInfo() const {
    const uint64 count = instructions ? instructions->Count() : 0;
    return {
        .rows = count,
        .total = totalInstructions,
        .dropped = totalInstructions > count ? totalInstructions - count : 0,
    };
}

void SH2Tracer::Attached() {
    execAnalyst.Clear();
}

void SH2Tracer::Detached() {
    execAnalyst.Clear();
}

void SH2Tracer::Reset(uint32 pc, uint32 sp, bool watchdogInitiated) {
    execAnalyst.Reset(pc, sp);
}

void SH2Tracer::ExecuteInstruction(uint32 pc, uint16 opcode, bool delaySlot) {
    if (!traceInstructions || !instructions) {
        return;
    }

    instructions->Write({currentEmulationFrame, pc, opcode, delaySlot});
    ++totalInstructions;
}

void SH2Tracer::DelaySlot(uint32 pc, uint32 target) {
    execAnalyst.DelaySlot(pc, target);
}

void SH2Tracer::Branch(uint32 pc, uint32 target) {
    execAnalyst.Branch(pc, target);
}

void SH2Tracer::BranchDelay(uint32 target) {
    execAnalyst.BranchDelay(target);
}

void SH2Tracer::Call(uint32 target) {
    execAnalyst.Call(target);
}

void SH2Tracer::Return(uint32 target) {
    execAnalyst.Return(target);
}

void SH2Tracer::ReturnFromException(uint32 target, uint32 newSP) {
    execAnalyst.ReturnFromException(target, newSP);
}

void SH2Tracer::Interrupt(uint8 vecNum, uint8 level, sh2::InterruptSource source, uint32 pc) {
    if (!traceInterrupts) {
        return;
    }

    interrupts.Write({vecNum, level, source, pc, m_interruptCounter++});
}

void SH2Tracer::Exception(uint8 vecNum, uint32 oldPC, uint32 oldSR, uint32 oldSP, uint32 newPC) {
    execAnalyst.Exception(vecNum, oldPC, oldSP, newPC);

    if (!traceExceptions) {
        return;
    }

    exceptions.Write({vecNum, oldPC, oldSR});
}

void SH2Tracer::Trap(uint8 vecNum, uint32 oldPC, uint32 oldSP, uint32 newPC) {
    execAnalyst.Trap(vecNum, oldPC, oldSP, newPC);
}

void SH2Tracer::ChangeStack(uint32 newSP) {
    execAnalyst.ChangeStack(newSP);
}

void SH2Tracer::ResizeStack(uint32 oldSP, uint32 newSP) {
    execAnalyst.ResizeStack(oldSP, newSP);
}

void SH2Tracer::PushRegisterToStack(uint8 rn, uint32 oldSP, uint32 newSP) {
    execAnalyst.PushRegisterToStack(rn, oldSP, newSP);
}

void SH2Tracer::PushToStack(ymir::debug::SH2StackValueType type, uint32 newSP) {
    execAnalyst.PushToStack(type, newSP);
}

void SH2Tracer::PopFromStack(uint32 newSP) {
    execAnalyst.PopFromStack(newSP);
}

void SH2Tracer::Begin32x32Division(sint32 dividend, sint32 divisor, bool overflowIntrEnable) {
    if (!traceDivisions) {
        return;
    }

    divisions.Write({.dividend = dividend,
                     .divisor = divisor,
                     .overflowIntrEnable = overflowIntrEnable,
                     .finished = false,
                     .div64 = false,
                     .counter = m_divisionCounter++});

    ++divStats.div32s;
}

void SH2Tracer::Begin64x32Division(sint64 dividend, sint32 divisor, bool overflowIntrEnable) {
    if (!traceDivisions) {
        return;
    }

    divisions.Write({.dividend = dividend,
                     .divisor = divisor,
                     .overflowIntrEnable = overflowIntrEnable,
                     .finished = false,
                     .div64 = true,
                     .counter = m_divisionCounter++});

    ++divStats.div64s;
}

void SH2Tracer::EndDivision(sint32 quotient, sint32 remainder, bool overflow) {
    if (!traceDivisions) {
        return;
    }

    auto &div = divisions.GetLast();
    if (div.finished) {
        return;
    }

    div.quotient = quotient;
    div.remainder = remainder;
    div.overflow = overflow;
    div.finished = true;

    if (overflow) {
        ++divStats.overflows;
        if (div.overflowIntrEnable) {
            ++divStats.interrupts;
        }
    }
}

void SH2Tracer::DMAXferBegin(uint32 channel, uint32 srcAddress, uint32 dstAddress, uint32 count, uint32 unitSize,
                             sint32 srcInc, sint32 dstInc) {
    if (!traceDMA) {
        return;
    }

    dmaTransfers[channel].Write({
        .srcAddress = srcAddress,
        .dstAddress = dstAddress,
        .count = count,
        .unitSize = unitSize,
        .srcInc = srcInc,
        .dstInc = dstInc,
        .finished = false,
        .counter = m_dmaCounter[channel]++,
    });

    ++dmaStats[channel].numTransfers;
}

void SH2Tracer::DMAXferData(uint32 channel, uint32 srcAddress, uint32 dstAddress, uint32 data, uint32 unitSize) {
    if (!traceDMA) {
        return;
    }

    auto &xfer = dmaTransfers[channel].GetLast();
    if (xfer.finished) {
        return;
    }

    // TODO: store transfer units in a shared/limited buffer or write directly to disk

    // auto &unit = xfer.units.emplace_back();
    // unit.srcAddress = srcAddress;
    // unit.dstAddress = dstAddress;
    // unit.data = data;
    // unit.unitSize = unitSize;

    dmaStats[channel].bytesTransferred += std::min(unitSize, 4u); // 16-byte transfers send four 4-byte units
}

void SH2Tracer::DMAXferEnd(uint32 channel, bool irqRaised) {
    if (!traceDMA) {
        return;
    }

    auto &xfer = dmaTransfers[channel].GetLast();
    if (xfer.finished) {
        return;
    }

    xfer.finished = true;
    xfer.irqRaised = irqRaised;

    if (irqRaised) {
        ++dmaStats[channel].interrupts;
    }
}

} // namespace app
