#include "sdrjo/util/geolocate.hpp"

#include <cstring>
#include <string>

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
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t kInvalidSocket = -1;
  static void closeSocket(socket_t s) { close(s); }
  static bool initSockets() { return true; }
#endif

namespace sdrjo {

// Cerca "chiave":valore nel JSON (piatto) di ip-api.com.
static bool jsonNumber(const std::string& body, const char* key, double& out)
{
    std::string pat = "\"" + std::string(key) + "\":";
    size_t p = body.find(pat);
    if (p == std::string::npos) return false;
    return std::sscanf(body.c_str() + p + pat.size(), "%lf", &out) == 1;
}

static std::string jsonString(const std::string& body, const char* key)
{
    std::string pat = "\"" + std::string(key) + "\":\"";
    size_t p = body.find(pat);
    if (p == std::string::npos) return {};
    size_t start = p + pat.size();
    size_t end = body.find('"', start);
    if (end == std::string::npos) return {};
    return body.substr(start, end - start);
}

bool parseGeoIpJson(const std::string& body, GeoIpResult& out)
{
    // ip-api.com risponde {"status":"success","lat":41.9,"lon":12.5,...}
    // oppure {"status":"fail","message":"..."}.
    if (jsonString(body, "status") == "fail") {
        out.ok = false;
        out.error = jsonString(body, "message");
        if (out.error.empty()) out.error = "risposta di errore dal servizio";
        return false;
    }
    double lat = 0, lon = 0;
    if (!jsonNumber(body, "lat", lat) || !jsonNumber(body, "lon", lon)) {
        out.ok = false;
        out.error = "lat/lon assenti nella risposta";
        return false;
    }
    out.ok = true;
    out.latDeg = lat;
    out.lonDeg = lon;
    out.city = jsonString(body, "city");
    return true;
}

GeoIpResult geolocateByIp()
{
    GeoIpResult res;
    if (!initSockets()) {
        res.error = "inizializzazione socket fallita";
        return res;
    }

    static const char* kHost = "ip-api.com";
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addrs = nullptr;
    if (getaddrinfo(kHost, "80", &hints, &addrs) != 0 || !addrs) {
        res.error = "DNS non raggiungibile (sei connesso a internet?)";
        return res;
    }

    socket_t s = kInvalidSocket;
    for (addrinfo* a = addrs; a; a = a->ai_next) {
        s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kInvalidSocket) continue;
        // Timeout di 5 s su connessione e lettura: il bottone della GUI
        // gira in un thread suo, ma non deve restare appeso per sempre.
#if defined(_WIN32)
        DWORD tmo = 5000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tmo, sizeof(tmo));
#else
        timeval tmo{5, 0};
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tmo, sizeof(tmo));
#endif
        if (connect(s, a->ai_addr, int(a->ai_addrlen)) == 0) break;
        closeSocket(s);
        s = kInvalidSocket;
    }
    freeaddrinfo(addrs);
    if (s == kInvalidSocket) {
        res.error = "connessione a ip-api.com fallita";
        return res;
    }

    const std::string req =
        "GET /json/?fields=status,message,lat,lon,city HTTP/1.1\r\n"
        "Host: ip-api.com\r\n"
        "User-Agent: SdrJo\r\n"
        "Connection: close\r\n\r\n";
    if (send(s, req.c_str(), int(req.size()), 0) <= 0) {
        closeSocket(s);
        res.error = "invio della richiesta fallito";
        return res;
    }

    std::string resp;
    char buf[2048];
    for (;;) {
        int n = int(recv(s, buf, sizeof(buf), 0));
        if (n <= 0) break;
        resp.append(buf, size_t(n));
        if (resp.size() > 64 * 1024) break; // risposta anomala
    }
    closeSocket(s);

    size_t bodyAt = resp.find("\r\n\r\n");
    if (bodyAt == std::string::npos) {
        res.error = "risposta HTTP non valida";
        return res;
    }
    parseGeoIpJson(resp.substr(bodyAt + 4), res);
    return res;
}

} // namespace sdrjo
