//
// Test dell'uscita SBS/BaseStation: formato delle righe e trasmissione
// TCP reale a un client collegato.
//
#include <sdrjo/adsb/sbs_output.hpp>
#include "test_util.hpp"

#include <chrono>
#include <cstring>
#include <thread>

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

int main()
{
    // --- Formato delle righe -----------------------------------------------
    {
        ModeSMessage m;
        CHECK(decodeHex("8D4840D6202CC371C32CE0576098", m));
        auto lines = toSbsLines(m);
        CHECK(lines.size() == 1);
        CHECK(lines[0].rfind("MSG,1,1,1,4840D6,1,", 0) == 0);
        CHECK(lines[0].find(",KLM1023,") != std::string::npos);

        CHECK(decodeHex("8D485020994409940838175B284F", m));
        lines = toSbsLines(m);
        CHECK(lines.size() == 1);
        CHECK(lines[0].rfind("MSG,4,1,1,485020,1,", 0) == 0);
        CHECK(lines[0].find(",159.2,182.9,") != std::string::npos);
        CHECK(lines[0].find(",-832,") != std::string::npos);

        // Posizione: emessa solo con la posizione risolta dal tracker.
        CHECK(decodeHex("8D40621D58C382D690C8AC2863A7", m));
        CHECK(toSbsLines(m).empty());
        Position pos{52.2572, 3.9194};
        lines = toSbsLines(m, &pos, 38000);
        CHECK(lines.size() == 1);
        CHECK(lines[0].rfind("MSG,3,1,1,40621D,1,", 0) == 0);
        CHECK(lines[0].find(",38000,") != std::string::npos);
        CHECK(lines[0].find(",52.25720,3.91940,") != std::string::npos);
    }

    // --- Trasmissione TCP end-to-end ---------------------------------------
    {
        SbsOutput sbs;
        CHECK(sbs.start(0)); // porta effimera

        // Collega un client.
#if defined(_WIN32)
        WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(sbs.port());
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(::connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0);

        // Aspetta che il server registri il client.
        for (int i = 0; i < 100 && sbs.clientCount() == 0; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        CHECK(sbs.clientCount() == 1);

        ModeSMessage m;
        CHECK(decodeHex("8D4840D6202CC371C32CE0576098", m));
        sbs.publish(m);

        char buf[512] = {0};
        int n = int(::recv(fd, buf, sizeof(buf) - 1, 0));
        CHECK(n > 0);
        CHECK(std::strstr(buf, "MSG,1,1,1,4840D6") != nullptr);
        CHECK(std::strstr(buf, "KLM1023") != nullptr);
        CHECK(std::strstr(buf, "\r\n") != nullptr);

#if defined(_WIN32)
        closesocket(fd);
#else
        close(fd);
#endif
        sbs.stop();
    }

    return testResult("test_sbs");
}
