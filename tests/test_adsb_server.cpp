//
// Test dell'interfaccia web ADS-B: JSON del tracker e server HTTP
// end-to-end (richiesta GET reale su loopback).
//
#include <sdrjo/adsb/adsb_server.hpp>
#include <sdrjo/adsb/mode_s.hpp>
#include "test_util.hpp"

#include <cstring>
#include <mutex>
#include <string>

#if defined(_WIN32)
  #include <winsock2.h>
  using socket_t = SOCKET;
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
#endif

using namespace sdrjo::adsb;

// Client HTTP minimo per il test.
static std::string httpGet(uint16_t port, const std::string& path)
{
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) return "";

    std::string req = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    ::send(fd, req.data(), int(req.size()), 0);

    std::string resp;
    char buf[4096];
    int n;
    while ((n = int(::recv(fd, buf, sizeof(buf), 0))) > 0)
        resp.append(buf, size_t(n));
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
    return resp;
}

int main()
{
    // Prepara un tracker con un aereo completo (posizione dalla coppia CPR).
    AircraftTracker tracker;
    ModeSMessage m;
    CHECK(decodeHex("8D4840D6202CC371C32CE0576098", m)); // callsign
    tracker.update(m);
    CHECK(decodeHex("8D40621D58C382D690C8AC2863A7", m)); // pos even
    tracker.update(m);
    CHECK(decodeHex("8D40621D58C386435CC412692AD6", m)); // pos odd
    tracker.update(m);
    CHECK(decodeHex("8D485020994409940838175B284F", m)); // velocita'
    tracker.update(m);

    // --- JSON ------------------------------------------------------------
    {
        auto json = aircraftToJson(tracker, Position{52.0, 4.0});
        CHECK(json.find("\"antenna\":{\"lat\":52.000000") != std::string::npos);
        CHECK(json.find("\"callsign\":\"KLM1023\"") != std::string::npos);
        CHECK(json.find("\"icao\":\"40621D\"") != std::string::npos);
        CHECK(json.find("\"altFt\":38000") != std::string::npos);
        // L'ultimo messaggio CPR e' l'odd: lat ~52.2657.
        CHECK(json.find("\"lat\":52.2") != std::string::npos);
        CHECK(json.find("\"distKm\":") != std::string::npos);
        CHECK(json.find("\"gsKt\":159") != std::string::npos);

        // Senza antenna: niente distanza.
        auto json2 = aircraftToJson(tracker, std::nullopt);
        CHECK(json2.find("\"antenna\":null") != std::string::npos);
        CHECK(json2.find("\"distKm\"") == std::string::npos);
    }

    // --- Server HTTP end-to-end -------------------------------------------
    {
        std::mutex mtx;
        AdsbWebServer web(tracker, mtx);
        web.setAntennaPosition(52.0, 4.0);
        CHECK(web.start(0)); // porta effimera
        CHECK(web.port() != 0);

        auto page = httpGet(web.port(), "/");
        CHECK(page.find("200 OK") != std::string::npos);
        CHECK(page.find("SdrJo") != std::string::npos);
        CHECK(page.find("leaflet") != std::string::npos);

        auto data = httpGet(web.port(), "/data/aircraft.json");
        CHECK(data.find("200 OK") != std::string::npos);
        CHECK(data.find("application/json") != std::string::npos);
        CHECK(data.find("KLM1023") != std::string::npos);

        auto missing = httpGet(web.port(), "/inesistente");
        CHECK(missing.find("404") != std::string::npos);

        web.stop();
        CHECK(!web.isRunning());
    }

    return testResult("test_adsb_server");
}
