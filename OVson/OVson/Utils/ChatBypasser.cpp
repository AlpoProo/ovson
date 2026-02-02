#include "ChatBypasser.h"
#include <unordered_map>

std::string ChatBypasser::process(const std::string& input) {
    // basic mapping for bypass
    static const std::unordered_map<char, std::string> mapping = {
        {'a', "á"}, {'b', "b"}, {'c', "c"}, {'d', "d"}, {'e', "é"},
        {'f', "f"}, {'g', "g"}, {'h', "h"}, {'i', "í"}, {'j', "j"},
        {'k', "k"}, {'l', "l"}, {'m', "m"}, {'n', "n"}, {'o', "ó"},
        {'p', "p"}, {'q', "q"}, {'r', "r"}, {'s', "s"}, {'t', "t"},
        {'u', "ú"}, {'v', "v"}, {'w', "w"}, {'x', "x"}, {'y', "ÿ"},
        {'z', "z"},
        {'A', "Á"}, {'B', "B"}, {'C', "C"}, {'D', "D"}, {'E', "É"},
        {'F', "F"}, {'G', "G"}, {'H', "H"}, {'I', "Í"}, {'J', "J"},
        {'K', "K"}, {'L', "L"}, {'M', "M"}, {'N', "N"}, {'O', "Ó"},
        {'P', "P"}, {'Q', "Q"}, {'R', "R"}, {'S', "S"}, {'T', "T"},
        {'U', "Ú"}, {'V', "V"}, {'W', "W"}, {'X', "X"}, {'Y', "Ÿ"},
        {'Z', "Z"}
    };

    std::string result;
    for (char ch : input) {
        if (mapping.count(ch)) {
            result += mapping.at(ch);
        } else {
            result += ch;
        }
    }
    return result;
}
