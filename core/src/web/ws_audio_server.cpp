#include "sdrjo/web/ws_audio_server.hpp"

#include "sdrjo/util/adpcm.hpp"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>

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
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t kInvalidSocket = -1;
  static void closeSocket(socket_t s) { close(s); }
  static bool initSockets() { return true; }
#endif

#if defined(MSG_NOSIGNAL)
  #define SEND_FLAGS_WS MSG_NOSIGNAL
#else
  #define SEND_FLAGS_WS 0
#endif

namespace sdrjo {

// ---------------------------------------------------------------------------
// SHA-1 (solo per il Sec-WebSocket-Accept del handshake: non e' crypto
// di sicurezza, e' il requisito del protocollo RFC 6455).
// ---------------------------------------------------------------------------
static void sha1(const uint8_t* data, size_t len, uint8_t out[20])
{
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476,
                     0xC3D2E1F0};
    uint64_t bitLen = uint64_t(len) * 8;

    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    for (int i = 7; i >= 0; i--) msg.push_back(uint8_t(bitLen >> (i * 8)));

    auto rol = [](uint32_t v, int s) { return (v << s) | (v >> (32 - s)); };

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = (uint32_t(msg[chunk + 4 * i]) << 24) |
                   (uint32_t(msg[chunk + 4 * i + 1]) << 16) |
                   (uint32_t(msg[chunk + 4 * i + 2]) << 8) |
                   uint32_t(msg[chunk + 4 * i + 3]);
        }
        for (int i = 16; i < 80; i++)
            w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                   k = 0xCA62C1D6; }
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    for (int i = 0; i < 5; i++) {
        out[4 * i] = uint8_t(h[i] >> 24);
        out[4 * i + 1] = uint8_t(h[i] >> 16);
        out[4 * i + 2] = uint8_t(h[i] >> 8);
        out[4 * i + 3] = uint8_t(h[i]);
    }
}

static std::string base64(const uint8_t* in, size_t n)
{
    static const char* kTab =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8) |
                     in[i + 2];
        out += kTab[(v >> 18) & 63];
        out += kTab[(v >> 12) & 63];
        out += kTab[(v >> 6) & 63];
        out += kTab[v & 63];
    }
    if (i + 1 == n) {
        uint32_t v = uint32_t(in[i]) << 16;
        out += kTab[(v >> 18) & 63];
        out += kTab[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == n) {
        uint32_t v = (uint32_t(in[i]) << 16) | (uint32_t(in[i + 1]) << 8);
        out += kTab[(v >> 18) & 63];
        out += kTab[(v >> 12) & 63];
        out += kTab[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

// ---------------------------------------------------------------------------

struct WsAudioServer::Client {
    socket_t fd = kInvalidSocket;
    std::thread thread;
    std::mutex qMutex;
    std::condition_variable qCv;
    std::deque<std::vector<uint8_t>> queue; // blocchi ADPCM da inviare
    std::atomic<bool> dead{false};
};

WsAudioServer::~WsAudioServer() { stop(); }

void WsAudioServer::setToken(const std::string& token)
{
    std::lock_guard<std::mutex> lk(mutex_);
    token_ = token;
}

std::string WsAudioServer::token() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return token_;
}

bool WsAudioServer::start(uint16_t port, bool bindAll)
{
    if (running_.load() || !initSockets()) return false;

    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalidSocket) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = bindAll ? INADDR_ANY : htonl(INADDR_LOOPBACK);
    if (bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0 ||
        listen(fd, 8) != 0) {
        closeSocket(fd);
        return false;
    }
    listenFd_ = intptr_t(fd);
    port_ = port;
    running_.store(true);
    acceptThread_ = std::thread(&WsAudioServer::acceptLoop, this);
    return true;
}

void WsAudioServer::stop()
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
    if (acceptThread_.joinable()) acceptThread_.join();
    if (listenFd_ != -1) {
        closeSocket(socket_t(listenFd_));
        listenFd_ = -1;
    }

    // I client vengono chiusi da chi li toglie dal registro: qui si
    // prendono tutti, si svegliano le recv/send con shutdown e si
    // chiude il socket solo DOPO il join (niente doppie chiusure).
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        clients.swap(clients_);
        for (auto& c : clients) {
            c->dead.store(true);
#if defined(_WIN32)
            shutdown(c->fd, SD_BOTH);
#else
            shutdown(c->fd, SHUT_RDWR);
#endif
            c->qCv.notify_all();
        }
    }
    for (auto& c : clients) {
        if (c->thread.joinable()) c->thread.join();
        if (c->fd != kInvalidSocket) closeSocket(c->fd);
    }
}

void WsAudioServer::acceptLoop()
{
    while (running_.load()) {
        sockaddr_in peer{};
#if defined(_WIN32)
        int plen = sizeof(peer);
#else
        socklen_t plen = sizeof(peer);
#endif
        socket_t c = accept(socket_t(listenFd_), (sockaddr*)&peer, &plen);
        if (c == kInvalidSocket) {
            if (!running_.load()) return;
            continue;
        }
        auto client = std::make_shared<Client>();
        client->fd = c;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            clients_.push_back(client);
        }
        client->thread = std::thread(&WsAudioServer::clientLoop, this,
                                     client);
    }
}

void WsAudioServer::clientLoop(std::shared_ptr<Client> c)
{
    // --- Handshake HTTP -> WebSocket ---
    std::string req;
    char buf[1024];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 8192) {
        int n = int(recv(c->fd, buf, sizeof(buf), 0));
        if (n <= 0) break;
        req.append(buf, size_t(n));
    }

    auto fail = [&](const char* status) {
        std::string resp = std::string("HTTP/1.1 ") + status +
                           "\r\nConnection: close\r\n\r\n";
        send(c->fd, resp.c_str(), int(resp.size()), SEND_FLAGS_WS);
        c->dead.store(true);
    };

    // Token nel query string (?token=...).
    std::string expected = token();
    bool tokenOk = expected.empty();
    if (!tokenOk) {
        size_t at = req.find("token=");
        if (at != std::string::npos) {
            size_t end = req.find_first_of(" &\r\n", at);
            std::string got = req.substr(at + 6, end - (at + 6));
            tokenOk = (got == expected);
        }
    }

    size_t keyAt = req.find("Sec-WebSocket-Key:");
    if (!tokenOk) {
        fail("403 Forbidden");
    } else if (req.find("\r\n\r\n") == std::string::npos ||
               keyAt == std::string::npos) {
        fail("400 Bad Request");
    } else {
        size_t start = keyAt + 18;
        while (start < req.size() && req[start] == ' ') start++;
        size_t end = req.find("\r\n", start);
        std::string key = req.substr(start, end - start);
        key += "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        uint8_t digest[20];
        sha1((const uint8_t*)key.data(), key.size(), digest);
        std::string resp =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + base64(digest, 20) + "\r\n\r\n";
        if (send(c->fd, resp.c_str(), int(resp.size()), SEND_FLAGS_WS) <= 0)
            c->dead.store(true);
    }

    // --- Invio dei blocchi audio come frame binari ---
    while (!c->dead.load() && running_.load()) {
        std::vector<uint8_t> block;
        {
            std::unique_lock<std::mutex> lk(c->qMutex);
            c->qCv.wait_for(lk, std::chrono::milliseconds(200), [&] {
                return !c->queue.empty() || c->dead.load() ||
                       !running_.load();
            });
            if (c->queue.empty()) continue;
            block = std::move(c->queue.front());
            c->queue.pop_front();
        }
        // Header frame: FIN + binario, lunghezza a 16 bit (i blocchi da
        // 20 ms sono ~484 byte, sempre sotto i 64 KiB).
        uint8_t hdr[4] = {0x82, 126, uint8_t(block.size() >> 8),
                          uint8_t(block.size() & 0xFF)};
        if (send(c->fd, (const char*)hdr, 4, SEND_FLAGS_WS) <= 0 ||
            send(c->fd, (const char*)block.data(), int(block.size()),
                 SEND_FLAGS_WS) <= 0) {
            c->dead.store(true);
        }
    }

    c->dead.store(true);

    // Si toglie dal registro e chiude il socket, ma solo se e' ancora
    // registrato: se stop() lo ha gia' preso in carico, sara' stop() a
    // chiudere dopo il join (mai due proprietari dello stesso fd).
    std::lock_guard<std::mutex> lk(mutex_);
    for (size_t i = 0; i < clients_.size(); i++) {
        if (clients_[i].get() == c.get()) {
            closeSocket(c->fd);
            c->fd = kInvalidSocket;
            c->thread.detach(); // il thread e' questo stesso
            clients_.erase(clients_.begin() + ptrdiff_t(i));
            break;
        }
    }
}

void WsAudioServer::pushAudio(const float* mono, size_t n)
{
    if (!running_.load()) return;
    pending_.insert(pending_.end(), mono, mono + n);

    constexpr size_t kBlock = 960; // 20 ms a 48 kHz
    size_t at = 0;
    while (pending_.size() - at >= kBlock) {
        auto block = adpcmEncodeBlock(pending_.data() + at, kBlock);
        at += kBlock;
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& c : clients_) {
            if (c->dead.load()) continue;
            std::lock_guard<std::mutex> qlk(c->qMutex);
            // Coda limitata: se il client e' lento perde audio vecchio
            // (meglio un buco che latenza che cresce senza limite).
            if (c->queue.size() > 50) c->queue.pop_front();
            c->queue.push_back(block);
            c->qCv.notify_one();
        }
    }
    pending_.erase(pending_.begin(), pending_.begin() + ptrdiff_t(at));
    if (pending_.size() > 48000) pending_.clear(); // paranoia
}

size_t WsAudioServer::clientCount() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    size_t n = 0;
    for (auto& c : clients_)
        if (!c->dead.load()) n++;
    return n;
}

} // namespace sdrjo
