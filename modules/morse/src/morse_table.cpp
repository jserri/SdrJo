#include "sdrjo/morse/morse_table.hpp"

#include <cctype>
#include <map>

namespace sdrjo::morse {

static const std::map<std::string, char>& table()
{
    static const std::map<std::string, char> t = {
        {".-", 'A'},    {"-...", 'B'},  {"-.-.", 'C'},  {"-..", 'D'},
        {".", 'E'},     {"..-.", 'F'},  {"--.", 'G'},   {"....", 'H'},
        {"..", 'I'},    {".---", 'J'},  {"-.-", 'K'},   {".-..", 'L'},
        {"--", 'M'},    {"-.", 'N'},    {"---", 'O'},   {".--.", 'P'},
        {"--.-", 'Q'},  {".-.", 'R'},   {"...", 'S'},   {"-", 'T'},
        {"..-", 'U'},   {"...-", 'V'},  {".--", 'W'},   {"-..-", 'X'},
        {"-.--", 'Y'},  {"--..", 'Z'},
        {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'},
        {"....-", '4'}, {".....", '5'}, {"-....", '6'}, {"--...", '7'},
        {"---..", '8'}, {"----.", '9'},
        {".-.-.-", '.'}, {"--..--", ','}, {"..--..", '?'}, {"-..-.", '/'},
        {"-....-", '-'}, {".--.-.", '@'}, {"-.--.", '('},  {"-.--.-", ')'},
        {"---...", ':'}, {".-.-.", '+'},  {"-...-", '='},
    };
    return t;
}

char symbolToChar(const std::string& symbol)
{
    auto it = table().find(symbol);
    return (it != table().end()) ? it->second : '\0';
}

std::string charToSymbol(char c)
{
    char up = char(std::toupper(static_cast<unsigned char>(c)));
    for (const auto& [sym, ch] : table()) {
        if (ch == up) return sym;
    }
    return "";
}

} // namespace sdrjo::morse
