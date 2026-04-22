#pragma once
#include <string>
#include <vector>

namespace Utils {
    static const char* b64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    inline std::string Base64Encode(const std::vector<unsigned char>& data) {
        std::string out;
        int val = 0, valb = -6;
        for (unsigned char c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                out.push_back(b64_table[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) out.push_back(b64_table[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }

    inline std::vector<unsigned char> Base64Decode(const std::string& data) {
        std::vector<unsigned char> out;
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[b64_table[i]] = i;

        int val = 0, valb = -8;
        for (unsigned char c : data) {
            if (T[c] == -1) break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                out.push_back((val >> valb) & 0xFF);
                valb -= 8;
            }
        }
        return out;
    }
}
