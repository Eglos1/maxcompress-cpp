#pragma once
// ============================================================================
// LZ77 Compression Algorithm
// Sliding-window dictionary compression (the basis of zlib/deflate)
// ============================================================================

#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <limits>

namespace mc {

struct LZ77Params {
    int windowSize  = 32768;  // max back-reference distance
    int maxMatchLen = 258;    // max match length
    int minMatchLen = 3;      // minimum match to encode as reference
};

class LZ77 {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input,
                                         const LZ77Params& p = {}) {
        std::vector<uint8_t> output;
        if (input.empty()) return output;

        // Header: original size (4 bytes)
        uint32_t origSize = (uint32_t)input.size();
        output.resize(4);
        memcpy(output.data(), &origSize, 4);

        size_t pos = 0;
        while (pos < input.size()) {
            int bestLen = 0;
            int bestDist = 0;

            size_t searchStart = (pos > (size_t)p.windowSize) ? pos - p.windowSize : 0;

            for (size_t d = searchStart; d < pos; ++d) {
                int len = 0;
                while (pos + len < input.size() && len < p.maxMatchLen &&
                       input[d + len] == input[pos + len]) {
                    len++;
                }
                if (len > bestLen) {
                    bestLen = len;
                    bestDist = (int)(pos - d);
                }
            }

            if (bestLen >= p.minMatchLen) {
                // Encode as (dist, len) reference
                // flag byte: high bit set = reference, low 3 bits = extra len bits
                int extraLen = bestLen - p.minMatchLen;
                uint8_t flag = 0x80;
                if (extraLen >= 7) {
                    flag |= 7;
                    output.push_back(flag);
                    uint8_t extra = (uint8_t)(extraLen - 7);
                    output.push_back(extra);
                } else {
                    flag |= (uint8_t)extraLen;
                    output.push_back(flag);
                }
                // Distance: 2 bytes little-endian
                output.push_back((uint8_t)(bestDist & 0xFF));
                output.push_back((uint8_t)((bestDist >> 8) & 0xFF));
                pos += bestLen;
            } else {
                // Literal byte - prefix with 0x00 escape to distinguish from references
                output.push_back(0x00);
                output.push_back(input[pos]);
                pos++;
            }
        }

        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        std::vector<uint8_t> output;
        if (input.size() < 4) return output;

        uint32_t origSize;
        memcpy(&origSize, input.data(), 4);
        output.reserve(origSize);

        size_t pos = 4;
        while (pos < input.size() && output.size() < origSize) {
            uint8_t flag = input[pos++];
            if (flag == 0x00) {
                // Literal byte (escaped)
                if (pos < input.size()) {
                    output.push_back(input[pos++]);
                }
            } else if (flag & 0x80) {
                // Reference
                int extraLen = flag & 0x7;
                if (extraLen == 7 && pos < input.size()) {
                    extraLen += input[pos++];
                }
                int matchLen = 3 + extraLen;
                if (pos + 1 >= input.size()) break;
                int dist = input[pos] | (input[pos + 1] << 8);
                pos += 2;

                size_t srcPos = output.size() - dist;
                for (int i = 0; i < matchLen && output.size() < origSize; ++i) {
                    output.push_back(output[srcPos + i]);
                }
            }
        }

        return output;
    }
};

} // namespace mc
