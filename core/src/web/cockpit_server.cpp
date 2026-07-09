#include "sdrjo/web/cockpit_server.hpp"

#include <cstdio>

namespace sdrjo {

CockpitServer::CockpitServer()
{
    server_.route("/", "text/html; charset=utf-8",
                  [] { return std::string(cockpitPageHtml()); });
    server_.route("/api/status", "application/json",
                  [this] { return statusJson(); });
    server_.route("/api/spectrum", "application/json",
                  [this] { return spectrumJson(); });
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
    {
        std::lock_guard<std::mutex> lk(devMutex_);
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "\"device\":{\"name\":\"%s\",\"freqHz\":%.0f,"
                      "\"rateHz\":%.0f},",
                      deviceName_.c_str(), freqHz_, rateHz_);
        j += buf;
    }
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
