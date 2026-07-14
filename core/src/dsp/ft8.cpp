#include "sdrjo/dsp/ft8.hpp"

#include "sdrjo/dsp/fft.hpp"
#include "ft8/ft8_tables.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <complex>

namespace sdrjo::dsp::ft8 {
namespace {

// --- Costanti di protocollo (fisse per FT8) --------------------------------
constexpr int kNsym = 79;       // simboli di canale
constexpr int kNdata = 58;      // simboli dati
constexpr int kCostas[7] = {3, 1, 4, 0, 6, 5, 2};
constexpr int kGray[8] = {0, 1, 3, 2, 5, 6, 4, 7};   // tono = gray[valore]
constexpr double kToneSpacing = 6.25;                 // Hz
constexpr double kSymbolPeriod = 0.16;                // s
constexpr uint16_t kCrcPoly = 0x2757;

// --- Costanti FT4 ----------------------------------------------------------
constexpr int kFt4Nsym = 103;
constexpr int kFt4Ndata = 87;
constexpr int kCostas4[4][4] = {
    {0, 1, 3, 2}, {1, 0, 2, 3}, {2, 3, 1, 0}, {3, 2, 0, 1}};
constexpr int kGray4[4] = {0, 1, 3, 2};
constexpr double kFt4ToneSpacing = 12000.0 / 576.0;   // 20.833 Hz
constexpr double kFt4SymbolPeriod = 576.0 / 12000.0;  // 0.048 s
// Vettore di mescolamento applicato ai 77 bit prima della FEC (genft4).
constexpr uint8_t kRvec[77] = {
    0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0,1,0,0,1,0,1,1,0,
    0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1,0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,
    1,0,1};

// --- CRC-14 -----------------------------------------------------------------
// Calcolata sui 77 bit + 5 zeri di padding, poi 14 zeri di aumento (come in
// WSJT-X, boost augmented_crc<14,0x2757>).
uint16_t crc14(const uint8_t* bits, int nbits)
{
    uint32_t reg = 0;
    auto push = [&](int bit) {
        reg <<= 1;
        if (bit) reg |= 1;
        if (reg & (1u << 14)) reg ^= (1u << 14) | kCrcPoly;
    };
    for (int i = 0; i < nbits; i++) push(bits[i]);
    for (int i = 0; i < 5; i++) push(0);
    for (int i = 0; i < 14; i++) push(0);
    return uint16_t(reg & 0x3FFF);
}

// --- Matrice generatrice e connettivita' di parita' ------------------------
struct LdpcModel {
    uint8_t gen[83][91];       // matrice generatrice
    int nm[83][7];             // bit (0-based) di ciascun controllo, -1 pad
    int nrw[83];               // peso di ciascun controllo
    int mn[174][3];            // i 3 controlli che coinvolgono ciascun bit
    int edgeSlot[174][3];      // posizione del bit dentro la riga nm[]

    LdpcModel()
    {
        for (int i = 0; i < 83; i++) {
            const char* hex = kGeneratorHex[i];
            // 23 cifre hex -> 92 bit, uso i primi 91.
            int bitpos = 0;
            for (int c = 0; c < 23; c++) {
                int v = (hex[c] <= '9') ? hex[c] - '0'
                                        : (std::tolower(hex[c]) - 'a' + 10);
                for (int b = 3; b >= 0; b--) {
                    if (bitpos < 91) gen[i][bitpos] = uint8_t((v >> b) & 1);
                    bitpos++;
                }
            }
            nrw[i] = kNrw[i];
            for (int c = 0; c < 7; c++)
                nm[i][c] = (c < nrw[i]) ? (kNm[i][c] - 1) : -1;
        }
        // Costruisci mn[] (per ogni bit, i controlli che lo contengono).
        int count[174] = {0};
        for (int i = 0; i < 83; i++)
            for (int c = 0; c < nrw[i]; c++) {
                int j = nm[i][c];
                if (j >= 0 && j < 174 && count[j] < 3) {
                    mn[j][count[j]] = i;
                    edgeSlot[j][count[j]] = c;
                    count[j]++;
                }
            }
    }
};

const LdpcModel& model()
{
    static const LdpcModel m;
    return m;
}

// CRC valida su un blocco di 91 bit (77 messaggio + 14 CRC)?
bool crcOk(const uint8_t* cw91)
{
    uint16_t expected = 0;
    for (int i = 77; i < 91; i++) expected = uint16_t((expected << 1) | cw91[i]);
    return crc14(cw91, 77) == expected;
}

} // namespace

// --- LDPC encode ------------------------------------------------------------
void encode174(const uint8_t bits77[77], uint8_t codeword174[174])
{
    const LdpcModel& m = model();
    uint8_t msg[91];
    std::memcpy(msg, bits77, 77);
    uint16_t crc = crc14(bits77, 77);
    for (int i = 0; i < 14; i++) msg[77 + i] = uint8_t((crc >> (13 - i)) & 1);
    for (int i = 0; i < 91; i++) codeword174[i] = msg[i];
    for (int i = 0; i < 83; i++) {
        int s = 0;
        for (int k = 0; k < 91; k++) s += m.gen[i][k] & msg[k];
        codeword174[91 + i] = uint8_t(s & 1);
    }
}

// --- LDPC belief-propagation decode ----------------------------------------
// Traduzione fedele del decoder tanh-product (bpdecode174_91). LLR>0 = bit 1.
bool bpDecode(const float llr174[174], uint8_t bits77[77], int maxIter)
{
    const LdpcModel& m = model();
    double llr[174];
    for (int i = 0; i < 174; i++) llr[i] = llr174[i];

    double tov[174][3] = {{0}};
    uint8_t cw[174];

    int ncnt = 0, nclast = 0;
    for (int it = 0; it <= maxIter; it++) {
        double zn[174];
        for (int j = 0; j < 174; j++) {
            zn[j] = llr[j] + tov[j][0] + tov[j][1] + tov[j][2];
            cw[j] = zn[j] > 0 ? 1 : 0;
        }
        // sindrome
        int ncheck = 0;
        for (int i = 0; i < 83; i++) {
            int s = 0;
            for (int c = 0; c < m.nrw[i]; c++) s ^= cw[m.nm[i][c]];
            if (s) ncheck++;
        }
        if (ncheck == 0 && crcOk(cw)) {
            std::memcpy(bits77, cw, 77);
            return true;
        }
        if (it > 0) {
            if (ncheck - nclast < 0) ncnt = 0; else ncnt++;
            if (ncnt >= 5 && it >= 10 && ncheck > 15) return false;
        }
        nclast = ncheck;

        // messaggi bit->check: contributo totale meno cio' che il check ha dato
        double toc[83][7] = {{0}};
        for (int j = 0; j < 174; j++)
            for (int s = 0; s < 3; s++)
                toc[m.mn[j][s]][m.edgeSlot[j][s]] = zn[j] - tov[j][s];

        // messaggi check->bit (prodotto delle tangenti, escluso il bit)
        for (int j = 0; j < 174; j++) {
            for (int s = 0; s < 3; s++) {
                int chk = m.mn[j][s];
                int slot = m.edgeSlot[j][s];
                double prod = 1.0;
                for (int c = 0; c < m.nrw[chk]; c++) {
                    if (c == slot) continue;
                    prod *= std::tanh(-toc[chk][c] / 2.0);
                }
                double tmn = -prod;
                if (tmn > 0.9999999999) tmn = 0.9999999999;
                if (tmn < -0.9999999999) tmn = -0.9999999999;
                tov[j][s] = 2.0 * std::atanh(tmn);
            }
        }
    }
    return false;
}

// --- OSD: ordered-statistics decoding --------------------------------------
// Fallback del BP: rende sistematico il generatore sulle 91 posizioni piu'
// affidabili, poi prova i pattern d'errore di peso <= norder su quelle,
// scegliendo la codeword piu' vicina (con CRC valido).
bool osdDecode(const float llr174[174], uint8_t bits77[77], int norder)
{
    const LdpcModel& m = model();
    const int N = 174, K = 91;

    double absrx[174];
    uint8_t hdec[174];
    for (int i = 0; i < N; i++) {
        hdec[i] = llr174[i] >= 0 ? 1 : 0;
        absrx[i] = std::fabs(double(llr174[i]));
    }
    int order[174];
    for (int i = 0; i < N; i++) order[i] = i;
    std::sort(order, order + N,
              [&](int a, int b) { return absrx[a] > absrx[b]; });

    // genmrb[i][c] = G_FULL[i][order[c]] con G_FULL = [I91 | GEN^T].
    static thread_local std::vector<uint8_t> gm;
    gm.assign(size_t(K) * N, 0);
    auto G = [&](int i, int c) -> uint8_t& { return gm[size_t(i) * N + c]; };
    for (int i = 0; i < K; i++)
        for (int c = 0; c < N; c++) {
            int col = order[c];
            G(i, c) = (col < K) ? (i == col ? 1 : 0) : m.gen[col - K][i];
        }
    int indices[174];
    for (int c = 0; c < N; c++) indices[c] = order[c];

    // Eliminazione di Gauss su GF(2): identita' sulle prime K colonne.
    for (int d = 0; d < K; d++) {
        int col = -1;
        for (int c = d; c < N; c++)
            if (G(d, c)) { col = c; break; }
        if (col < 0) return false;
        if (col != d) {
            for (int i = 0; i < K; i++) std::swap(G(i, d), G(i, col));
            std::swap(indices[d], indices[col]);
        }
        for (int i = 0; i < K; i++) {
            if (i == d || !G(i, d)) continue;
            for (int c = 0; c < N; c++) G(i, c) ^= G(d, c);
        }
    }

    double absrxP[174];
    uint8_t hdecP[174];
    for (int c = 0; c < N; c++) {
        absrxP[c] = absrx[indices[c]];
        hdecP[c] = hdec[indices[c]];
    }
    // Codeword d'ordine 0: c0 = (hdecP[:K] @ genmrb) % 2.
    uint8_t c0[174];
    for (int j = 0; j < N; j++) {
        int s = 0;
        for (int i = 0; i < K; i++) s ^= (hdecP[i] & G(i, j));
        c0[j] = uint8_t(s);
    }
    auto dist = [&](const uint8_t* cw) {
        double d = 0;
        for (int c = 0; c < N; c++) if (cw[c] ^ hdecP[c]) d += absrxP[c];
        return d;
    };

    uint8_t best[174];
    std::memcpy(best, c0, N);
    double bestD = dist(c0);

    uint8_t cand[174];
    if (norder >= 1) {
        for (int i = 0; i < K; i++) {
            for (int j = 0; j < N; j++) cand[j] = c0[j] ^ G(i, j);
            double d = dist(cand);
            if (d < bestD) { bestD = d; std::memcpy(best, cand, N); }
        }
    }
    if (norder >= 2) {
        for (int i = 0; i < K; i++)
            for (int j = i + 1; j < K; j++) {
                for (int k = 0; k < N; k++)
                    cand[k] = c0[k] ^ G(i, k) ^ G(j, k);
                double d = dist(cand);
                if (d < bestD) { bestD = d; std::memcpy(best, cand, N); }
            }
    }

    // Riporta la codeword nell'ordine originale e verifica il CRC.
    uint8_t cw[174];
    for (int c = 0; c < N; c++) cw[indices[c]] = best[c];
    if (!crcOk(cw)) return false;
    std::memcpy(bits77, cw, 77);
    return true;
}

namespace {

// --- Alfabeti per l'impacchettamento callsign ------------------------------
const char* A1 = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";   // 37
const char* A2 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";     // 36
const char* A3 = "0123456789";                                // 10
const char* A4 = " ABCDEFGHIJKLMNOPQRSTUVWXYZ";               // 27
const char* kText = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ+-./?";

constexpr long kNTokens = 2063592L;
constexpr long kMax22 = 4194304L;
constexpr long kMaxGrid4 = 32400L;

int findChar(const char* set, char c)
{
    const char* p = std::strchr(set, c);
    return p ? int(p - set) : -1;
}

bool callOk(const std::string& w)
{
    if (w.size() < 3 || w[0] == 'Q') return false;
    int i0 = 0;
    for (int i = int(w.size()); i > 0; i--)
        if (std::isdigit((unsigned char)w[i - 1])) { i0 = i; break; }
    if (i0 != 2 && i0 != 3) return false;
    std::string pfx = w.substr(0, i0 - 1);
    std::string sfx = w.substr(i0, 3);
    bool anyAlpha = false;
    for (char c : pfx) if (std::isalpha((unsigned char)c)) anyAlpha = true;
    if (!anyAlpha) return false;
    for (char c : sfx) if (!std::isalpha((unsigned char)c)) return false;
    return true;
}

// 28 bit -> callsign/token
bool unpack28(long n28, std::string& out)
{
    if (n28 < kNTokens) {
        if (n28 == 0) { out = "DE"; return true; }
        if (n28 == 1) { out = "QRZ"; return true; }
        if (n28 == 2) { out = "CQ"; return true; }
        if (n28 <= 1002) {
            char b[16];
            std::snprintf(b, sizeof(b), "CQ_%03ld", n28 - 3);
            out = b; return true;
        }
        if (n28 <= 532443) {
            long n = n28 - 1003;
            int div[4] = {27 * 27 * 27, 27 * 27, 27, 1};
            std::string s = "CQ_";
            for (int d = 0; d < 4; d++) { long j = n / div[d]; n %= div[d]; s += A4[j]; }
            // rimuovi spazi
            std::string t; for (char c : s) if (c != ' ') t += c;
            out = t; return true;
        }
        return false;
    }
    n28 -= kNTokens;
    if (n28 < kMax22) { out = "<...>"; return true; } // hash non risolto
    long n = n28 - kMax22;
    int i1 = int(n / (36L * 10 * 27 * 27 * 27)); n %= 36L * 10 * 27 * 27 * 27;
    int i2 = int(n / (10L * 27 * 27 * 27)); n %= 10L * 27 * 27 * 27;
    int i3 = int(n / (27 * 27 * 27)); n %= 27 * 27 * 27;
    int i4 = int(n / (27 * 27)); n %= 27 * 27;
    int i5 = int(n / 27); int i6 = int(n % 27);
    if (i1 < 0 || i1 >= 37 || i2 < 0 || i2 >= 36) return false;
    char c[7] = {A1[i1], A2[i2], A3[i3], A4[i4], A4[i5], A4[i6], 0};
    std::string call(c);
    // togli spazi
    std::string t; for (char ch : call) if (ch != ' ') t += ch;
    if (!callOk(t)) return false;
    out = t; return true;
}

bool gridFromN(long n, std::string& out)
{
    int j1 = int(n / (18 * 10 * 10)); n %= 18 * 10 * 10;
    int j2 = int(n / (10 * 10)); n %= 10 * 10;
    int j3 = int(n / 10); int j4 = int(n % 10);
    if (j1 < 0 || j1 > 17 || j2 < 0 || j2 > 17) return false;
    char g[5] = {char('A' + j1), char('A' + j2), char('0' + j3), char('0' + j4), 0};
    out = g; return true;
}

long nFromGrid4(const std::string& g)
{
    return (g[0] - 'A') * 18L * 10 * 10 + (g[1] - 'A') * 10L * 10 +
           (g[2] - '0') * 10L + (g[3] - '0');
}

bool isGrid4(const std::string& w)
{
    if (w.size() != 4) return false;
    return w[0] >= 'A' && w[0] <= 'R' && w[1] >= 'A' && w[1] <= 'R' &&
           std::isdigit((unsigned char)w[2]) && std::isdigit((unsigned char)w[3]);
}

// --- token/callsign -> 28 bit ----------------------------------------------
bool pack28(const std::string& c13in, long& out)
{
    std::string c13 = c13in;
    for (auto& c : c13) c = char(std::toupper((unsigned char)c));
    if (c13 == "DE") { out = 0; return true; }
    if (c13 == "QRZ") { out = 1; return true; }
    if (c13 == "CQ") { out = 2; return true; }
    if (c13.rfind("CQ_", 0) == 0) {
        std::string tail = c13.substr(3);
        bool digits = !tail.empty() && tail.size() == 3;
        for (char c : tail) if (!std::isdigit((unsigned char)c)) digits = false;
        if (digits) { out = 3 + std::stol(tail); return true; }
    }
    // callsign standard
    int n = int(c13.size());
    int iarea = 1;
    for (int i = n; i > 1; i--)
        if (std::isdigit((unsigned char)c13[i - 1])) { iarea = i; break; }
    int nplet = 0, npdig = 0, nslet = 0;
    for (int i = 0; i < iarea - 1; i++) {
        if (std::isalpha((unsigned char)c13[i])) nplet++;
        if (std::isdigit((unsigned char)c13[i])) npdig++;
    }
    for (int i = iarea; i < n; i++)
        if (std::isalpha((unsigned char)c13[i])) nslet++;
    if (iarea < 2 || iarea > 3 || nplet == 0 || npdig >= iarea - 1 || nslet > 3)
        return false; // non standard: non supportato in pack (RX-only)
    std::string call = (iarea == 2) ? (" " + c13.substr(0, 5)) : c13.substr(0, 6);
    while (call.size() < 6) call += ' ';
    int i1 = findChar(A1, call[0]);
    int i2 = findChar(A2, call[1]);
    int i3 = findChar(A3, call[2]);
    int i4 = findChar(A4, call[3]);
    int i5 = findChar(A4, call[4]);
    int i6 = findChar(A4, call[5]);
    if (i1 < 0 || i2 < 0 || i3 < 0 || i4 < 0 || i5 < 0 || i6 < 0) return false;
    long n28 = 36L * 10 * 27 * 27 * 27 * i1 + 10L * 27 * 27 * 27 * i2 +
               27L * 27 * 27 * i3 + 27L * 27 * i4 + 27L * i5 + i6;
    out = (n28 + kNTokens + kMax22) & ((1L << 28) - 1);
    return true;
}

// --- testo libero (13 char) -------------------------------------------------
void packText77(const std::string& text, uint8_t bits[71])
{
    std::string w = text;
    for (auto& c : w) c = char(std::toupper((unsigned char)c));
    if (w.size() > 13) w = w.substr(0, 13);
    while (w.size() < 13) w = " " + w;
    // n = base-42, 13 cifre -> 71 bit. Uso interi a 128 bit emulati.
    // 42^13 < 2^71: accumulo in due parole a 64 bit.
    unsigned long long hi = 0, lo = 0;
    auto mulAdd = [&](unsigned long long mul, unsigned long long add) {
        // (hi:lo) = (hi:lo)*mul + add
        unsigned long long loLo = (lo & 0xFFFFFFFFull) * mul;
        unsigned long long loHi = (lo >> 32) * mul;
        unsigned long long newLo = (loLo & 0xFFFFFFFFull) + add;
        unsigned long long carry = (loLo >> 32) + loHi + (newLo >> 32);
        lo = (newLo & 0xFFFFFFFFull) | ((carry & 0xFFFFFFFFull) << 32);
        hi = hi * mul + (carry >> 32);
    };
    for (char c : w) {
        int j = findChar(kText, c);
        if (j < 0) j = 0;
        mulAdd(42, (unsigned long long)j);
    }
    for (int i = 0; i < 71; i++) {
        int bitIndex = 70 - i;
        unsigned long long word = (bitIndex >= 64) ? hi : lo;
        int shift = bitIndex % 64;
        bits[i] = uint8_t((word >> shift) & 1);
    }
}

std::string unpackText77(const uint8_t bits[71])
{
    unsigned long long hi = 0, lo = 0;
    for (int i = 0; i < 71; i++) {
        int bitIndex = 70 - i;
        if (bitIndex >= 64) hi |= (unsigned long long)bits[i] << (bitIndex - 64);
        else lo |= (unsigned long long)bits[i] << bitIndex;
    }
    // dividi ripetutamente per 42 (128 bit / 42).
    char out[14]; out[13] = 0;
    for (int pos = 12; pos >= 0; pos--) {
        // (hi:lo) /= 42, resto r
        unsigned long long rem = 0;
        unsigned long long parts[2] = {hi, lo};
        for (int p = 0; p < 2; p++) {
            unsigned long long cur = parts[p];
            unsigned long long q = 0;
            for (int b = 63; b >= 0; b--) {
                rem = (rem << 1) | ((cur >> b) & 1);
                if (rem >= 42) { rem -= 42; q |= (1ull << b); }
            }
            parts[p] = q;
        }
        hi = parts[0]; lo = parts[1];
        out[pos] = kText[rem];
    }
    // trim
    std::string s(out);
    size_t a = s.find_first_not_of(' ');
    size_t b = s.find_last_not_of(' ');
    if (a == std::string::npos) return "";
    return s.substr(a, b - a + 1);
}

int bitsToInt(const uint8_t* b, int n)
{
    int v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | b[i];
    return v;
}

void intToBits(long v, uint8_t* b, int n)
{
    for (int i = 0; i < n; i++) b[i] = uint8_t((v >> (n - 1 - i)) & 1);
}

std::vector<std::string> splitWords(const std::string& msg)
{
    std::vector<std::string> w;
    std::string cur;
    for (char c : msg) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) { w.push_back(cur); cur.clear(); } }
        else cur += char(std::toupper((unsigned char)c));
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

} // namespace

// --- pack77 / unpack77 ------------------------------------------------------
bool pack77(const std::string& message, uint8_t bits77[77])
{
    auto words = splitWords(message);
    // Prova messaggio standard: [c1] [c2] [R] [grid4|RRR|RR73|73|report]
    if (words.size() >= 2 && words.size() <= 4) {
        std::vector<std::string> w = words;
        bool ir = false;
        // "CQ XX CALL ..." -> CQ_XX
        if (w.size() >= 3 && w[0] == "CQ" && w[1].size() <= 4 &&
            std::all_of(w[1].begin(), w[1].end(),
                        [](char c){ return std::isalnum((unsigned char)c); }) &&
            !isGrid4(w[1])) {
            // lascia stare: forma CQ_ non necessaria per i casi comuni
        }
        long n28a, n28b;
        std::string c1 = w[0];
        std::string c2 = w[1];
        if (pack28(c1, n28a) && pack28(c2, n28b)) {
            size_t idx = 2;
            if (idx < w.size() && w[idx] == "R") { ir = true; idx++; }
            long igrid4;
            bool haveField = false;
            if (idx < w.size()) {
                std::string f = w[idx];
                // "R-09"/"R+05": prefisso R fuso col rapporto.
                if (f.size() >= 2 && f[0] == 'R' &&
                    (f[1] == '+' || f[1] == '-')) { ir = true; f = f.substr(1); }
                if (isGrid4(f)) { igrid4 = nFromGrid4(f); haveField = true; }
                else if (f == "RRR") { igrid4 = kMaxGrid4 + 2; haveField = true; }
                else if (f == "RR73") { igrid4 = kMaxGrid4 + 3; haveField = true; }
                else if (f == "73") { igrid4 = kMaxGrid4 + 4; haveField = true; }
                else if ((f[0] == '+' || f[0] == '-') && f.size() >= 2) {
                    int snr = std::atoi(f.c_str());
                    long irpt = snr + 35;
                    if (snr > 49) irpt = snr - 101 + 35; // simmetrico all'unpack
                    igrid4 = kMaxGrid4 + irpt; haveField = true;
                }
            } else { igrid4 = kMaxGrid4 + 1; haveField = true; } // solo due call
            if (haveField && idx + 1 >= w.size()) {
                uint8_t b[77] = {0};
                intToBits(n28a, b + 0, 28);
                b[28] = 0; // ipa
                intToBits(n28b, b + 29, 28);
                b[57] = 0; // ipb
                b[58] = ir ? 1 : 0;
                intToBits(igrid4, b + 59, 15);
                intToBits(1, b + 74, 3); // i3=1
                std::memcpy(bits77, b, 77);
                return true;
            }
        }
    }
    // Fallback: testo libero (i3=0, n3=0)
    uint8_t b[77] = {0};
    packText77(message, b);
    // n3 (71:74)=0, i3 (74:77)=0 gia' azzerati
    std::memcpy(bits77, b, 77);
    return true;
}

bool unpack77(const uint8_t bits77[77], std::string& out)
{
    int i3 = bitsToInt(bits77 + 74, 3);
    int n3 = (i3 == 0) ? bitsToInt(bits77 + 71, 3) : 0;

    if (i3 == 0 && n3 == 0) {
        out = unpackText77(bits77);
        return !out.empty();
    }
    if (i3 == 1 || i3 == 2) {
        long n28a = bitsToInt(bits77 + 0, 28);
        int ipa = bits77[28];
        long n28b = bitsToInt(bits77 + 29, 28);
        int ipb = bits77[57];
        int ir = bits77[58];
        long igrid4 = bitsToInt(bits77 + 59, 15);
        std::string c1, c2;
        if (!unpack28(n28a, c1)) return false;
        if (!unpack28(n28b, c2)) return false;
        if (c1.rfind("CQ_", 0) == 0) c1 = "CQ " + c1.substr(3);
        if (c1.find('<') == std::string::npos && c1.size() >= 3 && ipa)
            c1 += (i3 == 1) ? "/R" : "/P";
        if (c2.find('<') == std::string::npos && c2.size() >= 3 && ipb)
            c2 += (i3 == 1) ? "/R" : "/P";
        std::string msg;
        if (igrid4 <= kMaxGrid4) {
            std::string g;
            if (!gridFromN(igrid4, g)) return false;
            msg = c1 + " " + c2 + (ir ? " R " : " ") + g;
        } else {
            long irpt = igrid4 - kMaxGrid4;
            if (irpt == 1) msg = c1 + " " + c2;
            else if (irpt == 2) msg = c1 + " " + c2 + " RRR";
            else if (irpt == 3) msg = c1 + " " + c2 + " RR73";
            else if (irpt == 4) msg = c1 + " " + c2 + " 73";
            else {
                long isnr = irpt - 35;
                if (isnr > 50) isnr -= 101;
                char b[16];
                std::snprintf(b, sizeof(b), "%+03ld", isnr);
                msg = c1 + " " + c2 + (ir ? " R" : " ") + b;
            }
        }
        out = msg;
        return true;
    }
    // Tipi rari (DXpedition, contest, call non standard): non supportati.
    return false;
}

// --- Modem ------------------------------------------------------------------
void tonesFromBits(const uint8_t bits77[77], int tones[79])
{
    uint8_t cw[174];
    encode174(bits77, cw);
    for (int i = 0; i < 7; i++) {
        tones[i] = kCostas[i];
        tones[36 + i] = kCostas[i];
        tones[72 + i] = kCostas[i];
    }
    int k = 7;
    for (int j = 0; j < kNdata; j++) {
        if (j == 29) k += 7; // salta l'array Costas centrale
        int i = 3 * j;
        int idx = cw[i] * 4 + cw[i + 1] * 2 + cw[i + 2];
        tones[k] = kGray[idx];
        k++;
    }
}

std::vector<float> encodeAudio(const std::string& message, double f0Hz,
                               double sampleRate)
{
    uint8_t bits[77];
    if (!pack77(message, bits)) return {};
    // Verifica di andata/ritorno: se non si rilegge, messaggio non valido.
    std::string chk;
    if (!unpack77(bits, chk)) return {};
    int tones[79];
    tonesFromBits(bits, tones);
    int nsps = int(std::lround(sampleRate * kSymbolPeriod));
    std::vector<float> out(size_t(nsps) * kNsym);
    double phase = 0.0;
    const double twoPi = 6.283185307179586;
    for (int s = 0; s < kNsym; s++) {
        double f = f0Hz + tones[s] * kToneSpacing;
        double dphi = twoPi * f / sampleRate;
        for (int n = 0; n < nsps; n++) {
            out[size_t(s) * nsps + n] = float(std::sin(phase));
            phase += dphi;
            if (phase > twoPi) phase -= twoPi;
        }
    }
    return out;
}

// --- Decodifica di una finestra --------------------------------------------
namespace {

// Ricampiona linearmente a outRate Hz.
std::vector<float> resampleTo(const float* in, size_t n, double inRate,
                              double outRate)
{
    size_t outN = size_t(std::llround(double(n) * outRate / inRate));
    std::vector<float> out(outN);
    for (size_t j = 0; j < outN; j++) {
        double t = double(j) * inRate / outRate;
        size_t i0 = size_t(t);
        double frac = t - double(i0);
        float a = (i0 < n) ? in[i0] : 0.0f;
        float b = (i0 + 1 < n) ? in[i0 + 1] : a;
        out[j] = float(a + (b - a) * frac);
    }
    return out;
}

} // namespace

std::vector<Decode> decodeAudio(const float* audio, size_t n, double sampleRate,
                                double freqMin, double freqMax)
{
    std::vector<Decode> results;
    if (n == 0) return results;

    constexpr int kNsps = 2048;                 // campioni/simbolo a 12800 Hz
    constexpr int kStep = kNsps / 4;            // passo di sincronismo (512)
    const double binHz = 12800.0 / kNsps;       // 6.25 Hz

    std::vector<float> dd = resampleTo(audio, n, sampleRate, 12800.0);
    // Estendi a 15 s se serve.
    size_t need = size_t(15.0 * 12800.0);
    if (dd.size() < need) dd.resize(need, 0.0f);

    int nframes = int((dd.size() - kNsps) / kStep) + 1;
    if (nframes < kNsym * 4) return results;

    const int nbin = kNsps / 2 + 1;
    std::vector<cfloat> buf(kNsps);
    int invGray[8];
    for (int v = 0; v < 8; v++) invGray[kGray[v]] = v;
    int binLo = std::max(1, int(freqMin / binHz));
    int binHi = std::min(nbin - 8, int(freqMax / binHz));

    // Sottrae dai campioni un segnale gia' decodificato: per ogni simbolo,
    // ricostruisce il tono (dal valore del bin corrispondente) e lo toglie.
    // Cosi' i segnali deboli nascosti sotto quelli forti emergono al passaggio
    // successivo.
    auto subtractSignal = [&](int b, int frame0, const int tones[79]) {
        const double twoPi = 6.283185307179586;
        for (int s = 0; s < kNsym; s++) {
            size_t base = size_t(frame0 + 4 * s) * kStep;
            if (base + kNsps > dd.size()) continue;
            for (int i = 0; i < kNsps; i++) buf[i] = cfloat(dd[base + i], 0.0f);
            fft(buf.data(), kNsps);
            int k = b + tones[s];
            cfloat Z = buf[k];
            cfloat w(float(std::cos(twoPi * k / kNsps)),
                     float(std::sin(twoPi * k / kNsps)));
            cfloat ph(1.0f, 0.0f);
            for (int i = 0; i < kNsps; i++) {
                float re = Z.real() * ph.real() - Z.imag() * ph.imag();
                dd[base + i] -= float(2.0 / kNsps) * re;
                ph *= w;
            }
        }
    };

    std::vector<std::string> seen;  // messaggi gia' aggiunti
    const int maxPasses = 3;
    for (int pass = 0; pass < maxPasses; pass++) {
        // Spettrogramma di potenza dal segnale corrente.
        std::vector<std::vector<float>> power(
            nframes, std::vector<float>(nbin, 0.0f));
        for (int f = 0; f < nframes; f++) {
            size_t base = size_t(f) * kStep;
            for (int i = 0; i < kNsps; i++) buf[i] = cfloat(dd[base + i], 0.0f);
            fft(buf.data(), kNsps);
            for (int b = 0; b < nbin; b++)
                power[f][b] = buf[b].real() * buf[b].real() +
                              buf[b].imag() * buf[b].imag();
        }

        // Ricerca candidati (correlazione coi 3 array Costas).
        struct Cand { int bin; int frame0; float score; };
        std::vector<Cand> cands;
        int maxOff = 40;
        for (int b = binLo; b <= binHi; b++) {
            for (int off = 0; off <= maxOff; off++) {
                float sig = 0.0f, tot = 0.0f;
                auto addBlock = [&](int frameBase) {
                    for (int nsy = 0; nsy < 7; nsy++) {
                        int fr = frameBase + 4 * nsy;
                        if (fr >= nframes) return;
                        sig += power[fr][b + kCostas[nsy]];
                        for (int t = 0; t < 8; t++) tot += power[fr][b + t];
                    }
                };
                addBlock(off + 0);
                addBlock(off + 4 * 36);
                addBlock(off + 4 * 72);
                if (tot <= 0.0f) continue;
                cands.push_back({b, off, sig / (tot / 8.0f)});
            }
        }
        if (cands.empty()) break;
        std::sort(cands.begin(), cands.end(),
                  [](const Cand& a, const Cand& b){ return a.score > b.score; });
        std::vector<Cand> keep;
        for (const auto& c : cands) {
            if (c.score < 1.8f) break;
            bool dup = false;
            for (const auto& k : keep)
                if (std::abs(k.bin - c.bin) <= 1 &&
                    std::abs(k.frame0 - c.frame0) <= 2)
                    dup = true;
            if (!dup) keep.push_back(c);
            if (keep.size() >= 60) break;
        }

        struct Sub { int b, frame0; int tones[79]; };
        std::vector<Sub> subs;
        int foundThisPass = 0;

        for (const auto& c : keep) {
            int b = c.bin, frame0 = c.frame0;
            std::array<std::array<float, 8>, kNsym> mag;
            for (int s = 0; s < kNsym; s++) {
                size_t base = size_t(frame0 + 4 * s) * kStep;
                if (base + kNsps > dd.size()) { mag[s].fill(0.0f); continue; }
                for (int i = 0; i < kNsps; i++)
                    buf[i] = cfloat(dd[base + i], 0.0f);
                fft(buf.data(), kNsps);
                for (int t = 0; t < 8; t++) {
                    cfloat z = buf[b + t];
                    mag[s][t] = std::sqrt(z.real() * z.real() +
                                          z.imag() * z.imag());
                }
            }
            int nsync = 0;
            auto checkBlock = [&](int symBase) {
                for (int nsy = 0; nsy < 7; nsy++) {
                    int s = symBase + nsy;
                    int best = 0;
                    for (int t = 1; t < 8; t++)
                        if (mag[s][t] > mag[s][best]) best = t;
                    if (best == kCostas[nsy]) nsync++;
                }
            };
            checkBlock(0); checkBlock(36); checkBlock(72);
            if (nsync <= 6) continue;

            float llr[174];
            auto symLlr = [&](int s, int j) {
                for (int p = 0; p < 3; p++) {
                    float mx1 = -1e30f, mx0 = -1e30f;
                    for (int t = 0; t < 8; t++) {
                        int bit = (invGray[t] >> (2 - p)) & 1;
                        if (bit) { if (mag[s][t] > mx1) mx1 = mag[s][t]; }
                        else { if (mag[s][t] > mx0) mx0 = mag[s][t]; }
                    }
                    llr[3 * j + p] = mx1 - mx0;
                }
            };
            int j = 0;
            for (int s = 7; s <= 35; s++) symLlr(s, j++);
            for (int s = 43; s <= 71; s++) symLlr(s, j++);

            double mean = 0, var = 0;
            for (int i = 0; i < 174; i++) mean += llr[i];
            mean /= 174.0;
            for (int i = 0; i < 174; i++) var += (llr[i]-mean)*(llr[i]-mean);
            var /= 174.0;
            double sigma = var > 0 ? std::sqrt(var) : 1.0;
            for (int i = 0; i < 174; i++) llr[i] = float(2.83 * llr[i] / sigma);

            uint8_t bits[77];
            bool ok = bpDecode(llr, bits, 30);
            if (!ok) ok = osdDecode(llr, bits, 2);
            if (!ok) continue;
            std::string msg;
            if (!unpack77(bits, msg) || msg.empty()) continue;

            int tones[79];
            tonesFromBits(bits, tones);
            // Registra il segnale per la sottrazione (anche se gia' visto,
            // cosi' lo togliamo comunque dal segnale residuo).
            Sub sub; sub.b = b; sub.frame0 = frame0;
            std::memcpy(sub.tones, tones, sizeof(tones));
            subs.push_back(sub);

            if (std::find(seen.begin(), seen.end(), msg) != seen.end()) continue;
            seen.push_back(msg);
            foundThisPass++;

            double xsig = 0, xnoi = 0;
            for (int s = 0; s < kNsym; s++) {
                xsig += mag[s][tones[s]] * mag[s][tones[s]];
                xnoi += mag[s][(tones[s] + 4) % 8] * mag[s][(tones[s] + 4) % 8];
            }
            double arg = (xnoi > 0) ? (xsig / xnoi - 1.0) : 0.0;
            float snr = float(10.0 * std::log10(std::max(arg, 0.001)) - 26.0);
            if (snr < -25.0f) snr = -25.0f;

            Decode d;
            d.freqHz = b * binHz;
            d.dtSec = (frame0 * kStep) / 12800.0 - 0.5;
            d.snrDb = snr;
            d.sync = c.score;
            d.message = msg;
            results.push_back(d);
        }

        if (foundThisPass == 0) break;
        // Sottrai i segnali decodificati e ripeti (a caccia dei deboli).
        if (pass + 1 < maxPasses)
            for (const auto& s : subs) subtractSignal(s.b, s.frame0, s.tones);
    }

    std::sort(results.begin(), results.end(),
              [](const Decode& a, const Decode& b){ return a.sync > b.sync; });
    return results;
}

// ===========================================================================
// FT4
// ===========================================================================
void ft4TonesFromBits(const uint8_t bits77[77], int tones[103])
{
    uint8_t scr[77];
    for (int i = 0; i < 77; i++) scr[i] = bits77[i] ^ kRvec[i];
    uint8_t cw[174];
    encode174(scr, cw);
    int itmp[87];
    for (int i = 0; i < 87; i++) {
        int two = cw[2 * i] * 2 + cw[2 * i + 1];
        itmp[i] = kGray4[two];
    }
    for (int i = 0; i < 4; i++) {
        tones[i] = kCostas4[0][i];
        tones[33 + i] = kCostas4[1][i];
        tones[66 + i] = kCostas4[2][i];
        tones[99 + i] = kCostas4[3][i];
    }
    for (int i = 0; i < 29; i++) tones[4 + i] = itmp[i];
    for (int i = 0; i < 29; i++) tones[37 + i] = itmp[29 + i];
    for (int i = 0; i < 29; i++) tones[70 + i] = itmp[58 + i];
}

std::vector<float> encodeAudioFt4(const std::string& message, double f0Hz,
                                  double sampleRate)
{
    uint8_t bits[77];
    if (!pack77(message, bits)) return {};
    std::string chk;
    if (!unpack77(bits, chk)) return {};
    int tones[103];
    ft4TonesFromBits(bits, tones);
    int nsps = int(std::lround(sampleRate * kFt4SymbolPeriod));
    std::vector<float> out(size_t(nsps) * kFt4Nsym);
    double phase = 0.0;
    const double twoPi = 6.283185307179586;
    for (int s = 0; s < kFt4Nsym; s++) {
        double f = f0Hz + tones[s] * kFt4ToneSpacing;
        double dphi = twoPi * f / sampleRate;
        for (int k = 0; k < nsps; k++) {
            out[size_t(s) * nsps + k] = float(std::sin(phase));
            phase += dphi;
            if (phase > twoPi) phase -= twoPi;
        }
    }
    return out;
}

namespace {

// Ampiezze complesse dei 4 toni FT4 in un simbolo (DFT diretta a frequenza
// esatta, con ricorrenza di fase). start = campione iniziale del simbolo.
void ft4SymbolTones(const std::vector<float>& dd, size_t start, double f0,
                    double fs, double ts, std::complex<double> out[4])
{
    std::complex<double> acc[4] = {}, ph[4], w[4];
    const double twoPi = 6.283185307179586;
    for (int t = 0; t < 4; t++) {
        double f = f0 + t * ts;
        w[t] = std::polar(1.0, -twoPi * f / fs);
        ph[t] = std::complex<double>(1.0, 0.0);
    }
    const int nsps = 576;
    for (int k = 0; k < nsps; k++) {
        double x = (start + k < dd.size()) ? double(dd[start + k]) : 0.0;
        for (int t = 0; t < 4; t++) { acc[t] += x * ph[t]; ph[t] *= w[t]; }
    }
    for (int t = 0; t < 4; t++) out[t] = acc[t];
}

// Correlazione di sincronismo FT4 (16 simboli Costas) a (f0, start).
double ft4SyncScore(const std::vector<float>& dd, size_t start, double f0,
                    double fs, double ts)
{
    const int nsps = 576;
    double s = 0;
    auto block = [&](int symBase, int ci) {
        for (int i = 0; i < 4; i++) {
            std::complex<double> ton[4];
            size_t st = start + size_t(symBase + i) * nsps;
            ft4SymbolTones(dd, st, f0, fs, ts, ton);
            s += std::norm(ton[kCostas4[ci][i]]);
        }
    };
    block(0, 0); block(33, 1); block(66, 2); block(99, 3);
    return s;
}

} // namespace

std::vector<Decode> decodeAudioFt4(const float* audio, size_t n,
                                   double sampleRate, double freqMin,
                                   double freqMax)
{
    std::vector<Decode> results;
    if (n == 0) return results;
    const double fs = 12000.0;
    const int nsps = 576;
    const double ts = fs / nsps;   // 20.833 Hz

    std::vector<float> dd = resampleTo(audio, n, sampleRate, fs);
    size_t need = size_t(7.5 * fs);
    if (dd.size() < need) dd.resize(need, 0.0f);

    // Spettrogramma grezzo per la ricerca dei candidati (FFT 1024, Hann,
    // passo mezzo simbolo). Bin = 11.72 Hz.
    const int nfft = 1024;
    const int step = nsps / 2;     // 288
    const double binHz = fs / nfft;
    int nframes = int((dd.size() - nfft) / step) + 1;
    if (nframes < 2 * kFt4Nsym) return results;
    const int nbin = nfft / 2 + 1;
    std::vector<float> hann(nsps);
    for (int i = 0; i < nsps; i++)
        hann[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * i / (nsps - 1));
    std::vector<std::vector<float>> P(nframes, std::vector<float>(nbin, 0.0f));
    std::vector<cfloat> buf(nfft);
    for (int f = 0; f < nframes; f++) {
        size_t base = size_t(f) * step;
        for (int i = 0; i < nfft; i++)
            buf[i] = (i < nsps) ? cfloat(dd[base + i] * hann[i], 0.0f)
                                : cfloat(0.0f, 0.0f);
        fft(buf.data(), nfft);
        for (int b = 0; b < nbin; b++)
            P[f][b] = buf[b].real() * buf[b].real() + buf[b].imag() * buf[b].imag();
    }

    int binLo = std::max(1, int(freqMin / binHz));
    int binHi = std::min(nbin - 8, int(freqMax / binHz));
    int maxOff = nframes - 2 * (kFt4Nsym - 1) - 1;
    if (maxOff < 1) maxOff = 1;

    struct Cand { double f0; int off; float score; };
    std::vector<Cand> cands;
    const int syncPos[4] = {0, 33, 66, 99};
    for (int fb = binLo; fb <= binHi; fb++) {
        double f0 = fb * binHz;
        for (int off = 0; off < maxOff; off++) {
            float sig = 0, tot = 0;
            for (int blk = 0; blk < 4; blk++) {
                for (int i = 0; i < 4; i++) {
                    int sy = syncPos[blk] + i;
                    int fr = off + 2 * sy;
                    if (fr >= nframes) { sig = -1; break; }
                    for (int t = 0; t < 4; t++) {
                        int bin = int(std::lround((f0 + t * ts) / binHz));
                        if (bin < 0 || bin >= nbin) continue;
                        float p = P[fr][bin];
                        tot += p;
                        if (t == kCostas4[blk][i]) sig += p;
                    }
                }
            }
            if (sig < 0 || tot <= 0) continue;
            cands.push_back({f0, off, sig / (tot / 4.0f)});
        }
    }
    if (cands.empty()) return results;
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b){ return a.score > b.score; });
    std::vector<Cand> keep;
    for (const auto& c : cands) {
        if (c.score < 1.5f) break;
        bool dup = false;
        for (const auto& k : keep)
            if (std::abs(k.f0 - c.f0) < 15.0 && std::abs(k.off - c.off) <= 2)
                dup = true;
        if (!dup) keep.push_back(c);
        if (keep.size() >= 40) break;
    }

    int invGray4[4];
    for (int v = 0; v < 4; v++) invGray4[kGray4[v]] = v;

    for (const auto& c : keep) {
        // Affinamento fine di frequenza e tempo con DFT diretta.
        double bestF = c.f0;
        size_t bestStart = size_t(c.off) * step;
        double bestS = -1;
        for (int df = -12; df <= 12; df++) {
            double f0 = c.f0 + df * 2.0;
            for (int dtn = -2; dtn <= 2; dtn++) {
                long st = long(c.off) * step + long(dtn) * (nsps / 4);
                if (st < 0) continue;
                double s = ft4SyncScore(dd, size_t(st), f0, fs, ts);
                if (s > bestS) { bestS = s; bestF = f0; bestStart = size_t(st); }
            }
        }

        // Demodula i 103 simboli.
        std::array<std::array<double, 4>, kFt4Nsym> mag;
        for (int s = 0; s < kFt4Nsym; s++) {
            std::complex<double> ton[4];
            ft4SymbolTones(dd, bestStart + size_t(s) * nsps, bestF, fs, ts, ton);
            for (int t = 0; t < 4; t++) mag[s][t] = std::abs(ton[t]);
        }
        // Verifica di sincronismo.
        int nsync = 0;
        for (int blk = 0; blk < 4; blk++)
            for (int i = 0; i < 4; i++) {
                int sy = syncPos[blk] + i;
                int best = 0;
                for (int t = 1; t < 4; t++) if (mag[sy][t] > mag[sy][best]) best = t;
                if (best == kCostas4[blk][i]) nsync++;
            }
        if (nsync <= 9) continue;

        // Soft-bit: 2 bit per simbolo dati.
        float llr[174];
        auto symLlr = [&](int sy, int j) {
            for (int p = 0; p < 2; p++) {
                double mx1 = -1e30, mx0 = -1e30;
                for (int t = 0; t < 4; t++) {
                    int val = invGray4[t];
                    int bit = (val >> (1 - p)) & 1;
                    if (bit) { if (mag[sy][t] > mx1) mx1 = mag[sy][t]; }
                    else { if (mag[sy][t] > mx0) mx0 = mag[sy][t]; }
                }
                llr[2 * j + p] = float(mx1 - mx0);
            }
        };
        int j = 0;
        for (int i = 0; i < 29; i++) symLlr(4 + i, j++);
        for (int i = 0; i < 29; i++) symLlr(37 + i, j++);
        for (int i = 0; i < 29; i++) symLlr(70 + i, j++);

        double mean = 0, var = 0;
        for (int i = 0; i < 174; i++) mean += llr[i];
        mean /= 174.0;
        for (int i = 0; i < 174; i++) var += (llr[i] - mean) * (llr[i] - mean);
        var /= 174.0;
        double sigma = var > 0 ? std::sqrt(var) : 1.0;
        for (int i = 0; i < 174; i++) llr[i] = float(2.83 * llr[i] / sigma);

        uint8_t scr[77];
        bool ok = bpDecode(llr, scr, 30);
        if (!ok) ok = osdDecode(llr, scr, 2);
        if (!ok) continue;
        // Togli il mescolamento RVEC per tornare al messaggio.
        uint8_t bits[77];
        for (int i = 0; i < 77; i++) bits[i] = scr[i] ^ kRvec[i];
        std::string msg;
        if (!unpack77(bits, msg) || msg.empty()) continue;

        Decode d;
        d.freqHz = bestF;
        d.dtSec = double(bestStart) / fs - 0.5;
        d.snrDb = 0.0f;
        d.sync = c.score;
        d.message = msg;
        bool merged = false;
        for (auto& r : results)
            if (r.message == msg) { if (c.score > r.sync) r = d; merged = true; break; }
        if (!merged) results.push_back(d);
    }
    std::sort(results.begin(), results.end(),
              [](const Decode& a, const Decode& b){ return a.sync > b.sync; });
    return results;
}

} // namespace sdrjo::dsp::ft8
