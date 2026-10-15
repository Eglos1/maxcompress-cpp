#pragma once
// ============================================================================
// LZW Compression
// Dictionary-based compression (GIF/TIFF style)
// ============================================================================

#include <vector>
#include <cstdint>
#include <map>
#include <string>

namespace mc {

class LZW {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input, int maxBits = 12) {
        std::vector<uint8_t> output;
        if (input.empty()) return output;

        int maxDictSize = 1 << maxBits;

        uint32_t origSize = (uint32_t)input.size();
        output.resize(4);
        memcpy(output.data(), &origSize, 4);

        // Write maxBits immediately after header so decompressor can read it
        output.push_back((uint8_t)maxBits);

        // Initialize dictionary with single-byte entries
        std::map<std::vector<uint8_t>, int> dict;
        int nextCode = 256;
        for (int i = 0; i < 256; i++) {
            dict[ {(uint8_t)i} ] = i;
        }

        std::vector<uint8_t> current;
        int bitsUsed = 9;
        uint32_t bitBuf = 0;
        int bitCount = 0;

        auto emitCode = [&](int code) {
            bitBuf |= (code << bitCount);
            bitCount += bitsUsed;
            while (bitCount >= 8) {
                output.push_back((uint8_t)(bitBuf & 0xFF));
                bitBuf >>= 8;
                bitCount -= 8;
            }
        };

        for (auto b : input) {
            std::vector<uint8_t> next = current;
            next.push_back(b);

            if (dict.count(next)) {
                current = next;
            } else {
                emitCode(dict[current]);
                if (nextCode < maxDictSize) {
                    dict[next] = nextCode++;
                    if (nextCode > (1 << bitsUsed) && bitsUsed < maxBits) {
                        bitsUsed++;
                    }
                }
                current = {(uint8_t)b};
            }
        }

        if (!current.empty()) {
            emitCode(dict[current]);
        }

        // Flush remaining bits
        if (bitCount > 0) {
            output.push_back((uint8_t)(bitBuf & 0xFF));
        }

        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        if (input.size() < 5) return {};

        size_t pos = 0;
        uint32_t origSize;
        memcpy(&origSize, input.data(), 4); pos += 4;

        int maxBits = input[pos++];
        int maxDictSize = 1 << maxBits;

        std::vector<std::vector<uint8_t>> dict;
        dict.reserve(maxDictSize);
        for (int i = 0; i < 256; i++) {
            dict.push_back({(uint8_t)i});
        }

        int bitsUsed = 9;
        uint32_t bitBuf = 0;
        int bitCount = 0;

        auto readCode = [&]() -> int {
            while (bitCount < bitsUsed && pos < input.size()) {
                bitBuf |= ((uint32_t)input[pos++] << bitCount);
                bitCount += 8;
            }
            if (bitCount < bitsUsed) return -1;
            int code = bitBuf & ((1 << bitsUsed) - 1);
            bitBuf >>= bitsUsed;
            bitCount -= bitsUsed;
            return code;
        };

        std::vector<uint8_t> output;
        output.reserve(origSize);

        int prevCode = readCode();
        if (prevCode < 0 || prevCode >= (int)dict.size()) return {};
        output.insert(output.end(), dict[prevCode].begin(), dict[prevCode].end());

        while (output.size() < origSize) {
            int code = readCode();
            if (code < 0) break;

            std::vector<uint8_t> entry;
            if (code < (int)dict.size()) {
                entry = dict[code];
            } else if (code == (int)dict.size()) {
                entry = dict[prevCode];
                entry.push_back(entry[0]);
            } else {
                break;
            }

            output.insert(output.end(), entry.begin(), entry.end());

            if ((int)dict.size() < maxDictSize) {
                std::vector<uint8_t> newEntry = dict[prevCode];
                newEntry.push_back(entry[0]);
                dict.push_back(newEntry);
                if ((int)dict.size() >= (1 << bitsUsed) && bitsUsed < maxBits) {
                    bitsUsed++;
                }
            }

            prevCode = code;
        }

        output.resize(origSize);
        return output;
    }
};

} // namespace mc
