#include "link_cable_service.hpp"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <netdb.h>
    #include <netinet/tcp.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

namespace app::services {

namespace {

#ifdef _WIN32
    using Socket = SOCKET;
    constexpr Socket kInvalidSocket = INVALID_SOCKET;
    void CloseSocket(Socket socket) {
        if (socket != kInvalidSocket) {
            closesocket(socket);
        }
    }
    bool SetNonBlocking(Socket socket) {
        u_long enabled = 1;
        return ioctlsocket(socket, FIONBIO, &enabled) == 0;
    }
    bool IsInProgress() {
        const int error = WSAGetLastError();
        return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
    }
#else
    using Socket = int;
    constexpr Socket kInvalidSocket = -1;
    void CloseSocket(Socket socket) {
        if (socket != kInvalidSocket) {
            close(socket);
        }
    }
    bool SetNonBlocking(Socket socket) {
        const int flags = fcntl(socket, F_GETFL, 0);
        return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
    }
    bool IsInProgress() {
        return errno == EWOULDBLOCK || errno == EINPROGRESS;
    }
#endif

    int WaitSocket(Socket socket, bool read, bool write) {
        fd_set readSet;
        fd_set writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        if (read) {
            FD_SET(socket, &readSet);
        }
        if (write) {
            FD_SET(socket, &writeSet);
        }
        timeval timeout{0, 50'000};
        return select(static_cast<int>(socket + 1), read ? &readSet : nullptr, write ? &writeSet : nullptr, nullptr,
                      &timeout);
    }

} // namespace

LinkCableService::~LinkCableService() {
    Disconnect();
}

std::string LinkCableService::GetError() const {
    std::lock_guard lock{m_errorMutex};
    return m_error;
}

std::vector<std::string> LinkCableService::TakeNotifications() {
    std::lock_guard lock{m_notificationMutex};
    std::vector<std::string> notifications;
    notifications.swap(m_notifications);
    return notifications;
}

void LinkCableService::Listen(uint16_t port) {
    Start(Mode::Listen, {}, port);
}

void LinkCableService::Connect(std::string host, uint16_t port) {
    Start(Mode::Connect, std::move(host), port);
}

void LinkCableService::AutoConnectLocal(uint16_t port) {
    Start(Mode::AutoLocal, "127.0.0.1", port);
}

void LinkCableService::Start(Mode mode, std::string host, uint16_t port) {
    Disconnect();
    {
        std::lock_guard lock{m_errorMutex};
        m_error.clear();
    }
    m_stop.store(false, std::memory_order_release);
    m_state.store(mode == Mode::Listen ? State::Listening : State::Connecting, std::memory_order_release);
    m_thread = std::thread{[this, mode, host = std::move(host), port] { Run(mode, host, port); }};
}

void LinkCableService::Disconnect() {
    m_stop.store(true, std::memory_order_release);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    {
        std::lock_guard lock{m_txMutex};
        m_tx.clear();
    }
    {
        std::lock_guard lock{m_rxMutex};
        m_rx.clear();
        m_rxCount.store(0, std::memory_order_release);
    }
    m_state.store(State::Disconnected, std::memory_order_release);
}

void LinkCableService::Fail(std::string message) {
    {
        std::lock_guard lock{m_errorMutex};
        m_error = message;
    }
    const State previous = m_state.exchange(State::Error, std::memory_order_acq_rel);
    if (previous == State::Connected && !m_stop.load(std::memory_order_acquire)) {
        std::lock_guard lock{m_notificationMutex};
        m_notifications.push_back("Battle (Taisen) Cable disconnected: " + message);
    }
}

void LinkCableService::Transmit(uint8 value) {
    if (GetState() != State::Connected) {
        return;
    }
    bool overflow = false;
    {
        std::lock_guard lock{m_txMutex};
        if (m_tx.size() < kMaxQueuedBytes) {
            m_tx.push_back(value);
        } else {
            overflow = true;
        }
    }
    if (overflow) {
        Fail("Link-cable send queue overflow");
        m_stop.store(true, std::memory_order_release);
    }
}

std::optional<uint8> LinkCableService::Receive() {
    if (m_rxCount.load(std::memory_order_acquire) == 0) {
        return std::nullopt;
    }
    std::lock_guard lock{m_rxMutex};
    if (m_rx.empty()) {
        return std::nullopt;
    }
    const uint8 value = m_rx.front();
    m_rx.pop_front();
    m_rxCount.store(m_rx.size(), std::memory_order_release);
    return value;
}

void LinkCableService::Run(Mode mode, std::string host, uint16_t port) {
    if (mode != Mode::AutoLocal) {
        RunSession(mode, std::move(host), port);
        return;
    }

    // The checkbox owns the lifetime of the virtual cable. A peer disconnect
    // drops the current TCP session but leaves the local cable enabled, so an
    // auto-paired instance keeps waiting for its partner to return.
    while (!m_stop.load(std::memory_order_acquire)) {
        {
            std::lock_guard lock{m_txMutex};
            m_tx.clear();
        }
        {
            std::lock_guard lock{m_rxMutex};
            m_rx.clear();
            m_rxCount.store(0, std::memory_order_release);
        }
        m_state.store(State::Connecting, std::memory_order_release);
        RunSession(mode, host, port);
        if (!m_stop.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

void LinkCableService::RunSession(Mode mode, std::string host, uint16_t port) {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Fail("Could not initialize Winsock");
        return;
    }
#endif

    Socket listener = kInvalidSocket;
    Socket peer = kInvalidSocket;
    auto cleanup = [&] {
        CloseSocket(peer);
        CloseSocket(listener);
#ifdef _WIN32
        WSACleanup();
#endif
    };

    if (mode != Mode::Connect) {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == kInvalidSocket || !SetNonBlocking(listener)) {
            Fail("Could not create link-cable listener");
            cleanup();
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(mode == Mode::AutoLocal ? INADDR_LOOPBACK : INADDR_ANY);
        address.sin_port = htons(port);
        if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || listen(listener, 1) != 0) {
            if (mode == Mode::AutoLocal) {
                CloseSocket(listener);
                listener = kInvalidSocket;
                mode = Mode::Connect;
            } else {
                Fail("Could not bind link-cable port (already in use or blocked)");
                cleanup();
                return;
            }
        }
        if (listener != kInvalidSocket) {
            m_state.store(State::Listening, std::memory_order_release);
            while (!m_stop.load(std::memory_order_acquire)) {
                const int ready = WaitSocket(listener, true, false);
                if (ready < 0) {
                    Fail("Link-cable listener failed");
                    cleanup();
                    return;
                }
                if (ready > 0) {
                    peer = accept(listener, nullptr, nullptr);
                    if (peer != kInvalidSocket) {
                        break;
                    }
                }
            }
            CloseSocket(listener);
            listener = kInvalidSocket;
        }
    }
    if (mode == Mode::Connect) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        addrinfo *addresses = nullptr;
        const std::string service = std::to_string(port);
        if (getaddrinfo(host.c_str(), service.c_str(), &hints, &addresses) != 0 || addresses == nullptr) {
            Fail("Could not resolve link-cable host");
            cleanup();
            return;
        }
        peer = socket(addresses->ai_family, addresses->ai_socktype, addresses->ai_protocol);
        if (peer == kInvalidSocket || !SetNonBlocking(peer)) {
            freeaddrinfo(addresses);
            Fail("Could not create link-cable socket");
            cleanup();
            return;
        }
        const int result = connect(peer, addresses->ai_addr, static_cast<int>(addresses->ai_addrlen));
        const bool pending = result != 0 && IsInProgress();
        freeaddrinfo(addresses);
        if (result != 0 && !pending) {
            Fail("Could not connect to link-cable host");
            cleanup();
            return;
        }
        while (pending && !m_stop.load(std::memory_order_acquire)) {
            const int ready = WaitSocket(peer, false, true);
            if (ready < 0) {
                Fail("Link-cable connection failed");
                cleanup();
                return;
            }
            if (ready > 0) {
                int error = 0;
#ifdef _WIN32
                int length = sizeof(error);
#else
                socklen_t length = sizeof(error);
#endif
                if (getsockopt(peer, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&error), &length) != 0 ||
                    error != 0) {
                    Fail("Link-cable connection refused or unreachable");
                    cleanup();
                    return;
                }
                break;
            }
        }
    }

    if (m_stop.load(std::memory_order_acquire)) {
        cleanup();
        return;
    }
    if (peer == kInvalidSocket) {
        Fail("Link-cable connection failed");
        cleanup();
        return;
    }
    int noDelay = 1;
    setsockopt(peer, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&noDelay), sizeof(noDelay));
#ifdef SO_NOSIGPIPE
    setsockopt(peer, SOL_SOCKET, SO_NOSIGPIPE, &noDelay, sizeof(noDelay));
#endif
    SetNonBlocking(peer);
    m_state.store(State::Connected, std::memory_order_release);

    std::array<uint8, 256> buffer{};
    while (!m_stop.load(std::memory_order_acquire)) {
        bool hasOutput = false;
        {
            std::lock_guard lock{m_txMutex};
            hasOutput = !m_tx.empty();
        }
        fd_set readSet;
        fd_set writeSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_SET(peer, &readSet);
        if (hasOutput) {
            FD_SET(peer, &writeSet);
        }
        timeval timeout{0, 20'000};
        const int ready =
            select(static_cast<int>(peer + 1), &readSet, hasOutput ? &writeSet : nullptr, nullptr, &timeout);
        if (ready < 0) {
            Fail("Link-cable socket error");
            break;
        }
        if (ready == 0) {
            continue;
        }
        if (FD_ISSET(peer, &readSet)) {
            const int count = recv(peer, reinterpret_cast<char *>(buffer.data()), static_cast<int>(buffer.size()), 0);
            if (count < 0 && IsInProgress()) {
                continue;
            }
            if (count <= 0) {
                Fail("Link-cable peer disconnected");
                break;
            }
            std::lock_guard lock{m_rxMutex};
            if (m_rx.size() + static_cast<size_t>(count) > kMaxQueuedBytes) {
                Fail("Link-cable receive queue overflow");
                break;
            }
            m_rx.insert(m_rx.end(), buffer.begin(), buffer.begin() + count);
            m_rxCount.store(m_rx.size(), std::memory_order_release);
        }
        if (hasOutput && FD_ISSET(peer, &writeSet)) {
            size_t count = 0;
            {
                std::lock_guard lock{m_txMutex};
                count = std::min(m_tx.size(), buffer.size());
                std::copy_n(m_tx.begin(), count, buffer.begin());
            }
            if (count != 0) {
                int flags = 0;
#ifdef MSG_NOSIGNAL
                flags = MSG_NOSIGNAL;
#endif
                const int sent =
                    send(peer, reinterpret_cast<const char *>(buffer.data()), static_cast<int>(count), flags);
                if (sent < 0 && !IsInProgress()) {
                    Fail("Link-cable send failed");
                    break;
                }
                if (sent > 0) {
                    std::lock_guard lock{m_txMutex};
                    for (int i = 0; i < sent; ++i) {
                        m_tx.pop_front();
                    }
                }
            }
        }
    }
    cleanup();
}

} // namespace app::services
