#include "sdrjo/web/cockpit_server.hpp"
#include "sdrjo/util/band_plan.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace sdrjo {

CockpitServer::CockpitServer()
{
    server_.route("/", "text/html; charset=utf-8",
                  [](const std::string&) { return std::string(cockpitPageHtml()); });
    server_.route("/api/status", "application/json",
                  [this](const std::string&) { return statusJson(); });
    server_.route("/api/spectrum", "application/json",
                  [this](const std::string&) { return spectrumJson(); });
    server_.route("/api/control", "application/json",
                  [this](const std::string& q) { return controlJson(q); });
    server_.streamRoute("/api/audio.wav", "audio/wav",
                        [this](HttpServer::StreamWriter& w) { audioStream(w); });
    server_.route("/api/wsinfo", "application/json",
                  [this](const std::string&) {
                      std::lock_guard<std::mutex> lk(devMutex_);
                      return "{\"port\":" + std::to_string(wsPort_) +
                             ",\"token\":\"" + wsToken_ + "\"}";
                  });
}

void CockpitServer::setVfoInfo(double freqHz, const std::string& mode)
{
    std::lock_guard<std::mutex> lk(devMutex_);
    vfoHz_ = freqHz;
    vfoMode_ = mode;
}

// /api/control?freq=100000000&mode=NFM : applica sintonia e/o demodulatore.
std::string CockpitServer::controlJson(const std::string& query)
{
    auto param = [&](const std::string& key) -> std::string {
        std::string needle = key + "=";
        size_t p = 0;
        while (p < query.size()) {
            size_t amp = query.find('&', p);
            std::string kv = query.substr(p, amp == std::string::npos
                                                 ? std::string::npos
                                                 : amp - p);
            if (kv.rfind(needle, 0) == 0) {
                std::string v = kv.substr(needle.size());
                // decodifica minima: %20 e '+' come spazio
                std::string out;
                for (size_t i = 0; i < v.size(); i++) {
                    if (v[i] == '+') out += ' ';
                    else if (v[i] == '%' && i + 2 < v.size() + 1 &&
                             i + 2 <= v.size()) {
                        out += char(std::strtol(v.substr(i + 1, 2).c_str(),
                                                nullptr, 16));
                        i += 2;
                    } else out += v[i];
                }
                return out;
            }
            if (amp == std::string::npos) break;
            p = amp + 1;
        }
        return {};
    };

    bool okTune = true, okMode = true;
    std::string f = param("freq");
    if (!f.empty() && tune_) okTune = tune_(std::atof(f.c_str()));
    std::string m = param("mode");
    if (!m.empty() && mode_) okMode = mode_(m);

    return (okTune && okMode) ? "{\"ok\":true}" : "{\"ok\":false}";
}

// Streaming WAV PCM 16 bit mono 48 kHz senza fine: ogni client ha la sua
// coda riempita da pushAudio(); il browser lo suona con un tag <audio>.
void CockpitServer::audioStream(HttpServer::StreamWriter& w)
{
    auto queue = std::make_shared<RingBuffer<int16_t>>(1 << 16);
    {
        std::lock_guard<std::mutex> lk(audioMutex_);
        audioClients_.push_back(queue);
    }

    // Header WAV con lunghezza "infinita".
    const uint32_t rate = 48000;
    uint8_t hdr[44] = {0};
    auto put32 = [&](int off, uint32_t v) {
        for (int i = 0; i < 4; i++) hdr[off + i] = uint8_t(v >> (8 * i));
    };
    auto put16 = [&](int off, uint16_t v) {
        hdr[off] = uint8_t(v);
        hdr[off + 1] = uint8_t(v >> 8);
    };
    std::memcpy(hdr, "RIFF", 4);
    put32(4, 0xFFFFFFFFu);
    std::memcpy(hdr + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);          // PCM
    put16(22, 1);          // mono
    put32(24, rate);
    put32(28, rate * 2);   // byte/s
    put16(32, 2);          // block align
    put16(34, 16);         // bit
    std::memcpy(hdr + 36, "data", 4);
    put32(40, 0xFFFFFFFFu);

    if (w.write(hdr, sizeof(hdr))) {
        int16_t chunk[2400]; // 50 ms
        int idleMs = 0;
        while (w.alive() && idleMs < 30000) {
            size_t n = queue->read(chunk, 2400);
            if (n == 0) {
                // Niente audio (ascolto spento): manda silenzio per tenere
                // vivo il player, ma non all'infinito.
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                idleMs += 50;
                std::memset(chunk, 0, sizeof(chunk));
                if (!w.write(chunk, 2400 * 2)) break;
                continue;
            }
            idleMs = 0;
            if (!w.write(chunk, n * 2)) break;
        }
    }

    std::lock_guard<std::mutex> lk(audioMutex_);
    audioClients_.erase(std::remove(audioClients_.begin(),
                                    audioClients_.end(), queue),
                        audioClients_.end());
}

void CockpitServer::pushAudio(const float* mono, size_t n)
{
    std::lock_guard<std::mutex> lk(audioMutex_);
    if (audioClients_.empty()) return;
    static thread_local std::vector<int16_t> conv;
    conv.resize(n);
    for (size_t i = 0; i < n; i++) {
        float v = mono[i];
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        conv[i] = int16_t(v * 32000.0f);
    }
    for (auto& q : audioClients_) q->write(conv.data(), n);
}

void CockpitServer::setDeviceInfo(const std::string& name, double freqHz,
                                  double rateHz)
{
    std::lock_guard<std::mutex> lk(devMutex_);
    deviceName_ = name;
    freqHz_ = freqHz;
    rateHz_ = rateHz;
}

bool CockpitServer::start(uint16_t port, bool bindAll)
{
    return server_.start(port, bindAll);
}

void CockpitServer::stop() { server_.stop(); }

static void appendEscaped(std::string& out, const std::string& s)
{
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (uint8_t(c) >= 0x20) out += c;
    }
}

std::string CockpitServer::statusJson()
{
    std::string j = "{";
    double freq = 0, rate = 0;
    {
        std::lock_guard<std::mutex> lk(devMutex_);
        freq = freqHz_;
        rate = rateHz_;
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "\"device\":{\"name\":\"%s\",\"freqHz\":%.0f,"
                      "\"rateHz\":%.0f},",
                      deviceName_.c_str(), freqHz_, rateHz_);
        j += buf;
    }

    {
        std::lock_guard<std::mutex> lk(devMutex_);
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "\"vfo\":{\"freqHz\":%.0f,\"mode\":\"%s\"},",
                      vfoHz_, vfoMode_.c_str());
        j += buf;
    }

    // Bande radio visibili nello span corrente (per le guide sul grafico).
    j += "\"bands\":[";
    if (rate > 0) {
        bool first = true;
        char buf[192];
        for (const auto& b : bandsInRange(freq - rate / 2, freq + rate / 2)) {
            if (!first) j += ",";
            first = false;
            std::snprintf(buf, sizeof(buf),
                          "{\"low\":%.0f,\"high\":%.0f,\"name\":\"%s\","
                          "\"cat\":\"%s\"}",
                          b.lowHz, b.highHz, b.name, b.category);
            j += buf;
        }
    }
    j += "],";
    j += "\"modules\":[";
    if (status_) {
        bool first = true;
        for (const auto& m : status_()) {
            if (!first) j += ",";
            first = false;
            j += "{\"name\":\"";
            appendEscaped(j, m.name);
            j += "\",\"description\":\"";
            appendEscaped(j, m.description);
            j += "\",\"webPort\":" + std::to_string(m.webPort);
            j += ",\"status\":" +
                 (m.statusJson.empty() ? std::string("{}") : m.statusJson);
            j += "}";
        }
    }
    j += "]}";
    return j;
}

std::string CockpitServer::spectrumJson()
{
    std::vector<float> spec;
    if (spectrum_) spec = spectrum_();

    std::string j = "{\"db\":[";
    char buf[16];
    for (size_t i = 0; i < spec.size(); i++) {
        if (i) j += ",";
        std::snprintf(buf, sizeof(buf), "%.1f", double(spec[i]));
        j += buf;
    }
    j += "]}";
    return j;
}

} // namespace sdrjo
