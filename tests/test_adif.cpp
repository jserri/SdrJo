// Test del log ADIF: estrazione call/grid/rapporto dai messaggi FT8 e
// formato dei record.
#include "sdrjo/util/adif_log.hpp"
#include "test_util.hpp"

#include <string>

using namespace sdrjo;

static bool has(const std::string& hay, const std::string& needle)
{
    return hay.find(needle) != std::string::npos;
}

int main()
{
    // CQ con locatore: la trasmittente e' il primo callsign, poi il grid.
    {
        AdifSpot s;
        CHECK(parseFt8Message("CQ IZ0ABC JN61", s));
        CHECK(s.call == "IZ0ABC");
        CHECK(s.grid == "JN61");
        CHECK(s.rst.empty());
    }
    // CQ con direzione prima del callsign.
    {
        AdifSpot s;
        CHECK(parseFt8Message("CQ DX K1ABC FN42", s));
        CHECK(s.call == "K1ABC");
        CHECK(s.grid == "FN42");
    }
    // Scambio con rapporto R-09: la trasmittente e' il 2o callsign.
    {
        AdifSpot s;
        CHECK(parseFt8Message("W1AW K1ABC R-09", s));
        CHECK(s.call == "K1ABC");
        CHECK(s.rst == "-09");
        CHECK(s.grid.empty());
    }
    // 73 non e' un rapporto ne' un grid.
    {
        AdifSpot s;
        CHECK(parseFt8Message("W1AW IZ0ABC 73", s));
        CHECK(s.call == "IZ0ABC");
        CHECK(s.rst.empty());
        CHECK(s.grid.empty());
    }
    // Testo libero senza callsign: non loggabile.
    {
        AdifSpot s;
        CHECK(!parseFt8Message("HELLO WORLD", s));
    }

    // Banda dalla frequenza.
    CHECK(adifBand(14074000.0) == "20m");
    CHECK(adifBand(7074000.0) == "40m");
    CHECK(adifBand(10136000.0) == "30m");
    CHECK(adifBand(999000.0).empty());

    // Formato del record.
    {
        AdifSpot s; s.call = "IZ0ABC"; s.grid = "JN61";
        std::string rec = adifRecord(s, "FT8", 14074000.0, 1700000000);
        CHECK(has(rec, "<CALL:6>IZ0ABC"));
        CHECK(has(rec, "<GRIDSQUARE:4>JN61"));
        CHECK(has(rec, "<MODE:3>FT8"));
        CHECK(has(rec, "<BAND:3>20m"));
        CHECK(has(rec, "<QSO_DATE:8>"));
        CHECK(has(rec, "<EOR>"));
    }

    return testResult("adif");
}
