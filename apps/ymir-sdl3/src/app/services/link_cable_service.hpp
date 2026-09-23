#pragma once

#include <ymir/core/types.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace app::services {

/// @brief Connects a Saturn SH-2 SCI to another Ymir instance over TCP.
///
/// Socket I/O runs on a worker thread. The emulation thread only touches the
/// bounded byte queues, so a stalled peer cannot block emulation.
class LinkCableService {
public:
    enum class State { Disconnected, Listening, Connecting, Connected, Error };

    LinkCableService() = default;
    ~LinkCableService();

    LinkCableService(const LinkCableService &) = delete;
    LinkCableService &operator=(const LinkCableService &) = delete;

    void Listen(uint16_t port);
    void Connect(std::string host, uint16_t port);
    void AutoConnectLocal(uint16_t port);
    void Disconnect();

    State GetState() const {
        return m_state.load(std::memory_order_acquire);
    }
    std::string GetError() const;
    std::vector<std::string> TakeNotifications();

    void Transmit(uint8 value);
    std::optional<uint8> Receive();

private:
    enum class Mode { Listen, Connect, AutoLocal };

    static constexpr size_t kMaxQueuedBytes = 64 * 1024;

    void Start(Mode mode, std::string host, uint16_t port);
    void Run(Mode mode, std::string host, uint16_t port);
    void RunSession(Mode mode, std::string host, uint16_t port);
    void Fail(std::string message);

    std::atomic<State> m_state{State::Disconnected};
    std::atomic<bool> m_stop{false};
    std::thread m_thread;

    mutable std::mutex m_errorMutex;
    std::string m_error;
    std::mutex m_notificationMutex;
    std::vector<std::string> m_notifications;

    std::mutex m_txMutex;
    std::deque<uint8> m_tx;
    std::mutex m_rxMutex;
    std::deque<uint8> m_rx;
    std::atomic<size_t> m_rxCount{0};
};

} // namespace app::services
