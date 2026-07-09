#include "sdrjo/util/http_server.hpp"

#include <cstring>
#include <vector>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static const socket_t kInvalidSocket = INVALID_SOCKET;
  static void closeSocket(socket_t s) { closesocket(s); }
  static bool initSockets()
  {
      static bool done = false;
      if (!done) {
          WSADATA wsa;
          if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
          done = true;
      }
      return true;
  }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t kInvalidSocket = -1;
  static void closeSocket(socket_t s) { close(s); }
  static bool initSockets() { return true; }
#endif

namespace sdrjo {

HttpServer::HttpServer() = default;

HttpServer::~HttpServer() { stop(); }

void HttpServer::route(const std::string& path, const std::string& contentType,
                       Handler handler)
{
    routes_[path] = Route{contentType, std::move(handler)};
}

static std::string base64Encode(const std::string& in)
{
    static const char* kTab =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < in.size()) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8) |
                     uint8_t(in[i + 2]);
        out += kTab[(v >> 18) & 63];
        out += kTab[(v >> 12) & 63];
        out += kTab[(v >> 6) & 63];
        out += kTab[v & 63];
        i += 3;
    }
    if (i + 1 == in.size()) {
        uint32_t v = uint8_t(in[i]) << 16;
        out += kTab[(v >> 18) & 63];
        out += kTab[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = (uint8_t(in[i]) << 16) | (uint8_t(in[i + 1]) << 8);
        out += kTab[(v >> 18) & 63];
        out += kTab[(v >> 12) & 63];
        out += kTab[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

void HttpServer::setAuth(const std::string& username,
                         const std::string& password)
{
    authToken_ = password.empty() ? std::string()
                                  : base64Encode(username + ":" + password);
}

bool HttpServer::start(uint16_t port, bool bindAll)
{
    if (running_.load()) return false;
    if (!initSockets()) return false;

    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalidSocket) return false;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = bindAll ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);

    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        ::listen(fd, 8) != 0) {
        closeSocket(fd);
        return false;
    }

    // Recupera la porta effettiva (utile con port = 0).
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
    worker_ = std::thread(&HttpServer::acceptLoop, this);
    return true;
}

void HttpServer::stop()
{
    if (!running_.exchange(false)) return;

    // Sblocca la accept() collegandosi a noi stessi, poi chiudi.
    socket_t wake = ::socket(AF_INET, SOCK_STREAM, 0);
    if (wake != kInvalidSocket) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::connect(wake, (sockaddr*)&addr, sizeof(addr));
        closeSocket(wake);
    }
    if (worker_.joinable()) worker_.join();
    closeSocket(socket_t(listenFd_));
    listenFd_ = -1;
}

void HttpServer::acceptLoop()
{
    while (running_.load()) {
        socket_t client = ::accept(socket_t(listenFd_), nullptr, nullptr);
        if (client == kInvalidSocket) break;
        if (!running_.load()) { closeSocket(client); break; }
        handleClient(intptr_t(client));
    }
}

void HttpServer::handleClient(intptr_t clientFd)
{
    socket_t client = socket_t(clientFd);

    char buf[2048];
    int rd = int(::recv(client, buf, sizeof(buf) - 1, 0));
    if (rd <= 0) { closeSocket(client); return; }
    buf[rd] = '\0';

    // Estrai il percorso dalla request line ("GET /path HTTP/1.1").
    std::string path;
    if (std::strncmp(buf, "GET ", 4) == 0) {
        const char* start = buf + 4;
        const char* end = std::strchr(start, ' ');
        if (end) path.assign(start, end);
        // Ignora la query string.
        auto q = path.find('?');
        if (q != std::string::npos) path.resize(q);
    }

    // Autenticazione (se configurata): confronto diretto del token Basic.
    if (!authToken_.empty()) {
        bool ok = false;
        if (const char* h = std::strstr(buf, "Authorization: Basic ")) {
            h += 21;
            std::string tok;
            while (*h && *h != '\r' && *h != '\n' && *h != ' ') tok += *h++;
            ok = (tok == authToken_);
        }
        if (!ok) {
            const char* resp =
                "HTTP/1.1 401 Unauthorized\r\n"
                "WWW-Authenticate: Basic realm=\"SdrJo\"\r\n"
                "Content-Length: 0\r\nConnection: close\r\n\r\n";
            ::send(client, resp, int(std::strlen(resp)), 0);
            closeSocket(client);
            return;
        }
    }

    std::string status = "404 Not Found";
    std::string contentType = "text/plain";
    std::string body = "not found";

    auto it = routes_.find(path);
    if (it != routes_.end()) {
        status = "200 OK";
        contentType = it->second.contentType;
        body = it->second.handler();
    }

    std::string resp = "HTTP/1.1 " + status + "\r\n"
        "Content-Type: " + contentType + "\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Cache-Control: no-store\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n" + body;

    size_t sent = 0;
    while (sent < resp.size()) {
        int n = int(::send(client, resp.data() + sent, int(resp.size() - sent), 0));
        if (n <= 0) break;
        sent += size_t(n);
    }
    closeSocket(client);
}

} // namespace sdrjo
