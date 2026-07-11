//
// Test del codec ADPCM e del server WebSocket audio:
//  - roundtrip ADPCM su un tono (SNR alto, blocchi autonomi)
//  - handshake RFC 6455 con il vettore di prova ufficiale
//  - rifiuto senza token, frame binari con audio decodificabile
//
#include <sdrjo/util/adpcm.hpp>
#include <sdrjo/web/ws_audio_server.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socket_t = SOCKET;
  static bool initSockets()
  {
      WSADATA w;
      return WSAStartup(MAKEWORD(2, 2), &w) == 0;
  }
  static void closeSocket(socket_t s) { closesocket(s); }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  using socket_t = int;
  static bool initSockets() { return true; }
  static void closeSocket(socket_t s) { close(s); }
#endif

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static socket_t connectTo(uint16_t port)
{
    socket_t s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) {
        closeSocket(s);
        return socket_t(-1);
    }
    // Timeout: il test non deve mai restare appeso su una recv.
#if defined(_WIN32)
    DWORD tmo = 2000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));
#else
    timeval tmo{2, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));
#endif
    return s;
}

static std::string recvSome(socket_t s, size_t atLeast, int tries = 50)
{
    std::string out;
    char buf[4096];
    while (out.size() < atLeast && tries-- > 0) {
        int n = int(recv(s, buf, sizeof(buf), 0));
        if (n <= 0) break;
        out.append(buf, size_t(n));
    }
    return out;
}

int main()
{
    initSockets();

    // --- ADPCM: roundtrip su un tono a 700 Hz ---
    {
        const size_t n = 960;
        std::vector<float> in(n);
        for (size_t i = 0; i < n; i++)
            in[i] = 0.6f * std::sin(2.0 * M_PI * 700.0 * double(i) / 48000.0);
        auto enc = sdrjo::adpcmEncodeBlock(in.data(), n);
        CHECK(enc.size() == 4 + n / 2);
        auto dec = sdrjo::adpcmDecodeBlock(enc.data(), enc.size());
        CHECK(dec.size() == n);
        double err = 0, ref = 0;
        for (size_t i = 40; i < n; i++) { // salta l'assestamento iniziale
            err += double(dec[i] - in[i]) * double(dec[i] - in[i]);
            ref += double(in[i]) * double(in[i]);
        }
        double snrDb = 10.0 * std::log10(ref / (err + 1e-12));
        std::printf("ADPCM SNR: %.1f dB\n", snrDb);
        CHECK(snrDb > 25.0);
        // Blocco malformato: nessun crash, vuoto.
        CHECK(sdrjo::adpcmDecodeBlock(enc.data(), 3).empty());
    }

    // --- Server WebSocket ---
    sdrjo::WsAudioServer ws;
    ws.setToken("segreto123");
    CHECK(ws.start(18751, false));

    // Senza token: 403.
    {
        socket_t s = connectTo(18751);
        CHECK(s != socket_t(-1));
        const char* req = "GET / HTTP/1.1\r\nHost: x\r\n"
                          "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                          "Sec-WebSocket-Version: 13\r\n\r\n";
        send(s, req, int(strlen(req)), 0);
        std::string resp = recvSome(s, 12);
        CHECK(resp.find("403") != std::string::npos);
        closeSocket(s);
    }

    // Con token: 101 e l'accept del vettore RFC 6455.
    socket_t s = connectTo(18751);
    CHECK(s != socket_t(-1));
    {
        const char* req = "GET /?token=segreto123 HTTP/1.1\r\nHost: x\r\n"
                          "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                          "Sec-WebSocket-Version: 13\r\n\r\n";
        send(s, req, int(strlen(req)), 0);
        std::string resp = recvSome(s, 100);
        CHECK(resp.find("101") != std::string::npos);
        // Valore atteso dall'esempio ufficiale dell'RFC 6455.
        CHECK(resp.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    }
    CHECK(ws.clientCount() == 1);

    // Spingi un tono: deve arrivare un frame binario con ADPCM valido.
    {
        std::vector<float> tone(960 * 3);
        for (size_t i = 0; i < tone.size(); i++)
            tone[i] =
                0.5f * std::sin(2.0 * M_PI * 700.0 * double(i) / 48000.0);
        ws.pushAudio(tone.data(), tone.size());

        std::string data = recvSome(s, 488);
        CHECK(data.size() >= 4);
        CHECK(uint8_t(data[0]) == 0x82); // FIN + frame binario
        CHECK(uint8_t(data[1]) == 126);  // lunghezza estesa a 16 bit
        size_t len = (uint8_t(data[2]) << 8) | uint8_t(data[3]);
        CHECK(len == 4 + 960 / 2);
        if (data.size() >= 4 + len) {
            auto pcm = sdrjo::adpcmDecodeBlock(
                (const uint8_t*)data.data() + 4, len);
            CHECK(pcm.size() == 960);
            double peak = 0;
            for (float v : pcm) peak = std::max(peak, std::fabs(double(v)));
            CHECK(peak > 0.3); // il tono c'e' davvero
        }
    }

    closeSocket(s);
    ws.stop();
    CHECK(!ws.isRunning());

    if (failures == 0) std::printf("test_ws_audio: OK\n");
    return failures == 0 ? 0 : 1;
}
