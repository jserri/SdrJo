//
// Test del Cockpit web: /api/status, /api/spectrum e pagina principale.
//
#include <sdrjo/web/cockpit_server.hpp>
#include "test_util.hpp"

#include <cstring>
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

static std::string httpGetAuth(uint16_t port, const std::string& path,
                               const std::string& basicToken);

static std::string httpGet(uint16_t port, const std::string& path)
{
    return httpGetAuth(port, path, "");
}

// GET che legge al massimo maxBytes e poi chiude (per gli stream infiniti).
static std::string httpGetLimited(uint16_t port, const std::string& path,
                                  size_t maxBytes)
{
#if defined(_WIN32)
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) return "";
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: x\r\n\r\n";
    ::send(fd, req.data(), int(req.size()), 0);
    std::string resp;
    char buf[4096];
    while (resp.size() < maxBytes) {
        int n = int(::recv(fd, buf, sizeof(buf), 0));
        if (n <= 0) break;
        resp.append(buf, size_t(n));
    }
#if defined(_WIN32)
    closesocket(fd);
#else
    close(fd);
#endif
    return resp;
}

static std::string httpGetAuth(uint16_t port, const std::string& path,
                               const std::string& basicToken)
{
#if defined(_WIN32)
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) return "";
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: x\r\n";
    if (!basicToken.empty())
        req += "Authorization: Basic " + basicToken + "\r\n";
    req += "\r\n";
    ::send(fd, req.data(), int(req.size()), 0);
    std::string resp;
    char buf[8192];
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
    sdrjo::CockpitServer cockpit;
    cockpit.setDeviceInfo("RTL-SDR test", 100.0e6, 2.4e6);
    cockpit.setStatusProvider([] {
        std::vector<sdrjo::CockpitServer::ModuleStatus> out;
        out.push_back({"ADS-B", "test \"desc\"", "{\"Aerei\":\"3\"}", 8757});
        return out;
    });
    cockpit.setSpectrumProvider([] {
        return std::vector<float>{-90.5f, -45.0f, -88.2f};
    });

    CHECK(cockpit.start(0));

    auto page = httpGet(cockpit.port(), "/");
    CHECK(page.find("200 OK") != std::string::npos);
    CHECK(page.find("SdrJo Cockpit") != std::string::npos);
    CHECK(page.find("api/spectrum") != std::string::npos);

    auto status = httpGet(cockpit.port(), "/api/status");
    CHECK(status.find("\"name\":\"RTL-SDR test\"") != std::string::npos);
    CHECK(status.find("\"freqHz\":100000000") != std::string::npos);
    CHECK(status.find("\"name\":\"ADS-B\"") != std::string::npos);
    CHECK(status.find("\\\"desc\\\"") != std::string::npos); // escape corretto
    CHECK(status.find("\"webPort\":8757") != std::string::npos);
    CHECK(status.find("{\"Aerei\":\"3\"}") != std::string::npos);

    auto spec = httpGet(cockpit.port(), "/api/spectrum");
    CHECK(spec.find("\"db\":[-90.5,-45.0,-88.2]") != std::string::npos);

    // --- Controllo remoto: /api/control ------------------------------------
    {
        double tunedTo = 0;
        std::string modeSet;
        cockpit.setTuneHandler([&](double f) { tunedTo = f; return true; });
        cockpit.setModeHandler([&](const std::string& m) {
            modeSet = m;
            return true;
        });
        auto r = httpGet(cockpit.port(),
                         "/api/control?freq=100300000&mode=WFM%20stereo");
        CHECK(r.find("{\"ok\":true}") != std::string::npos);
        CHECK(tunedTo == 100300000.0);
        CHECK(modeSet == "WFM stereo");

        // Lo stato espone il VFO.
        cockpit.setVfoInfo(100.3e6, "WFM stereo");
        auto st = httpGet(cockpit.port(), "/api/status");
        CHECK(st.find("\"vfo\":{\"freqHz\":100300000") != std::string::npos);
    }

    // --- Streaming audio: /api/audio.wav ------------------------------------
    {
        // Alimenta l'audio in un thread mentre il client legge.
        std::atomic<bool> feeding{true};
        std::thread feeder([&] {
            std::vector<float> tone(480);
            for (size_t i = 0; i < tone.size(); i++)
                tone[i] = 0.5f * std::sin(2.0 * 3.14159265 * 1000.0 *
                                          double(i) / 48000.0);
            while (feeding.load()) {
                cockpit.pushAudio(tone.data(), tone.size());
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });

        auto wav = httpGetLimited(cockpit.port(), "/api/audio.wav", 6000);
        feeding.store(false);
        feeder.join();

        CHECK(wav.find("200 OK") != std::string::npos);
        CHECK(wav.find("audio/wav") != std::string::npos);
        CHECK(wav.find("RIFF") != std::string::npos);
        CHECK(wav.find("WAVE") != std::string::npos);
        // Devono esserci campioni non nulli dopo l'header.
        size_t dataPos = wav.find("data");
        CHECK(dataPos != std::string::npos);
        bool nonZero = false;
        for (size_t i = dataPos + 8; i + 1 < wav.size(); i++)
            if (wav[i] != 0) nonZero = true;
        CHECK(nonZero);
    }

    cockpit.stop();

    // --- Autenticazione HTTP Basic -----------------------------------------
    {
        sdrjo::CockpitServer secured;
        secured.setPassword("segreta123");
        CHECK(secured.start(0));

        // Senza credenziali: 401.
        auto denied = httpGet(secured.port(), "/");
        CHECK(denied.find("401 Unauthorized") != std::string::npos);
        CHECK(denied.find("WWW-Authenticate") != std::string::npos);
        CHECK(denied.find("SdrJo Cockpit") == std::string::npos);

        // Con credenziali giuste: 200. base64("sdrjo:segreta123").
        auto ok = httpGetAuth(secured.port(), "/", "c2Ryam86c2VncmV0YTEyMw==");
        CHECK(ok.find("200 OK") != std::string::npos);
        CHECK(ok.find("SdrJo Cockpit") != std::string::npos);

        // Con credenziali sbagliate: 401.
        auto wrong = httpGetAuth(secured.port(), "/", "c2Ryam86c2JhZ2xpYXRh");
        CHECK(wrong.find("401 Unauthorized") != std::string::npos);

        secured.stop();
    }

    return testResult("test_cockpit");
}
