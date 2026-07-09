#include "sdrjo/util/tcp_line_server.hpp"

#include <cstring>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static const socket_t kInvalid = INVALID_SOCKET;
  static void closeSock(socket_t s) { closesocket(s); }
  static bool initSock()
  {
      static bool done = false;
      if (!done) { WSADATA w; if (WSAStartup(MAKEWORD(2, 2), &w) != 0) return false; done = true; }
      return true;
  }
  #define SEND_FLAGS 0
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t kInvalid = -1;
  static void closeSock(socket_t s) { close(s); }
  static bool initSock() { return true; }
  #if defined(MSG_NOSIGNAL)
    #define SEND_FLAGS MSG_NOSIGNAL
  #else
    #define SEND_FLAGS 0
  #endif
#endif

namespace sdrjo {

TcpLineServer::TcpLineServer() = default;
TcpLineServer::~TcpLineServer() { stop(); }

bool TcpLineServer::start(uint16_t port, bool bindAll)
{
    if (running_.load() || !initSock()) return false;

    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalid) return false;
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = bindAll ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 || ::listen(fd, 8) != 0) {
        closeSock(fd);
        return false;
    }

    sockaddr_in bound{};
#if defined(_WIN32)
    int blen = sizeof(bound);
#else
    socklen_t blen = sizeof(bound);
#endif
    getsockname(fd, (sockaddr*)&bound, &blen);
    port_ = ntohs(bound.sin_port);

    listenFd_ = intptr_t(fd);
    running_.store(true);
    worker_ = std::thread(&TcpLineServer::acceptLoop, this);
    return true;
}

void TcpLineServer::stop()
{
    if (!running_.exchange(false)) return;

    socket_t wake = ::socket(AF_INET, SOCK_STREAM, 0);
    if (wake != kInvalid) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::connect(wake, (sockaddr*)&addr, sizeof(addr));
        closeSock(wake);
    }
    if (worker_.joinable()) worker_.join();
    closeSock(socket_t(listenFd_));

    std::lock_guard<std::mutex> lk(clientsMutex_);
    for (auto c : clients_) closeSock(socket_t(c));
    clients_.clear();
}

void TcpLineServer::acceptLoop()
{
    while (running_.load()) {
        socket_t client = ::accept(socket_t(listenFd_), nullptr, nullptr);
        if (client == kInvalid) break;
        if (!running_.load()) { closeSock(client); break; }
        std::lock_guard<std::mutex> lk(clientsMutex_);
        clients_.push_back(intptr_t(client));
    }
}

void TcpLineServer::broadcast(const std::string& line)
{
    std::string data = line + "\r\n";
    std::lock_guard<std::mutex> lk(clientsMutex_);
    for (auto it = clients_.begin(); it != clients_.end();) {
        int n = int(::send(socket_t(*it), data.data(), int(data.size()), SEND_FLAGS));
        if (n <= 0) {
            closeSock(socket_t(*it));
            it = clients_.erase(it); // client disconnesso
        } else {
            ++it;
        }
    }
}

size_t TcpLineServer::clientCount() const
{
    std::lock_guard<std::mutex> lk(clientsMutex_);
    return clients_.size();
}

} // namespace sdrjo
