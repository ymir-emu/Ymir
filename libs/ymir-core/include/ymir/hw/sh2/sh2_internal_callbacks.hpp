#pragma once

/**
@file
@brief Internal callback definitions used by the SH2.
*/

#include <ymir/util/callback.hpp>
#include <ymir/core/types.hpp>

#include <optional>

namespace ymir::sh2 {

/// @brief Invoked when the SH2 acknowledges an external interrupt signal.
using CBAcknowledgeExternalInterrupt = util::RequiredCallback<void()>;

/// @brief Sends one byte from the SH-2 SCI to an external serial device.
using CBSerialTransmit = util::OptionalCallback<void(uint8)>;

/// @brief Retrieves a pending byte from an external serial device, if any.
using CBSerialReceive = util::OptionalCallback<std::optional<uint8>()>;

} // namespace ymir::sh2
