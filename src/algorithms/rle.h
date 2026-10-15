#pragma once
// ============================================================================
// Run-Length Encoding (RLE)
// Simple but effective for data with consecutive repeated bytes
// ============================================================================

#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace mc {

class RLE {
public:
    // Mode 0: Standard RLE with escape byte
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input) {
        std::vector<uint8_t> output;
        if (input.empty()) return output;

        uint32_t origSize = (uint32_t)input.size();
        output.resize(4);
        memcpy(output.data(), &origSize, 4);

        size_t i = 0;
        while (i < input.size()) {
            uint8_t val = input[i];
            int runLen = 1;
            while (i + runLen < input.size() && input[i + runLen] == val && runLen < 128) {
                runLen++;
            }

            if (runLen >= 3) {
                // Encode run: flag (high bit set + count), value
                output.push_back((uint8_t)(0x80 | (runLen - 1)));
                output.push_back(val);
            } else {
                // Encode literals
                size_t litStart = i;
                int litCount = runLen;
                // Look ahead for more literals
                while (i + litCount < input.size() && litCount < 128) {
                    uint8_t next = input[i + litCount];
                    int nextRun = 1;
                    while (i + litCount + nextRun < input.size() &&
                           input[i + litCount + nextRun] == next && nextRun < 3) {
                        nextRun++;
                    }
                    if (nextRun >= 3) break;
                    litCount++;
                }

                output.push_back((uint8_t)(litCount - 1)); // flag: low 7 bits = count
                for (int j = 0; j < litCount; j++) {
                    output.push_back(input[i + j]);
                }
                i += litCount;
                continue;
            }
            i += runLen;
        }

        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        if (input.size() < 4) return {};

        size_t pos = 0;
        uint32_t origSize;
        memcpy(&origSize, input.data(), 4); pos += 4;

        std::vector<uint8_t> output;
        output.reserve(origSize);

        while (pos < input.size() && output.size() < origSize) {
            uint8_t flag = input[pos++];
            if (flag & 0x80) {
                // Run
                int count = (flag & 0x7F) + 1;
                if (pos >= input.size()) break;
                uint8_t val = input[pos++];
                for (int i = 0; i < count && output.size() < origSize; i++) {
                    output.push_back(val);
                }
            } else {
                // Literals
                int count = (flag & 0x7F) + 1;
                for (int i = 0; i < count && pos < input.size() && output.size() < origSize; i++) {
                    output.push_back(input[pos++]);
                }
            }
        }

        return output;
    }
};

} // namespace mc
