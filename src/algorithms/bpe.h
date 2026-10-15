#pragma once
// ============================================================================
// Byte-Pair Encoding (BPE)
// Replaces most frequent byte pairs with unused bytes
// ============================================================================

#include <vector>
#include <cstdint>
#include <map>
#include <algorithm>
#include <cstring>

namespace mc {

class BPE {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input, int maxRounds = 5) {
        std::vector<uint8_t> output;
        if (input.empty()) return output;

        uint32_t origSize = (uint32_t)input.size();
        output.resize(4);
        memcpy(output.data(), &origSize, 4);

        std::vector<uint8_t> data(input.begin(), input.end());

        // Number of replacement rounds stored
        output.push_back((uint8_t)maxRounds);

        // Store replacement table at end
        struct Replacement {
            uint8_t pair[2];
            uint8_t replacement;
        };
        std::vector<Replacement> replacements;

        for (int round = 0; round < maxRounds && data.size() > 1; round++) {
            // Count pairs
            std::map<std::pair<uint8_t,uint8_t>, int> pairCount;
            for (size_t i = 0; i + 1 < data.size(); i++) {
                pairCount[{data[i], data[i+1]}]++;
            }

            if (pairCount.empty()) break;

            // Find most frequent pair
            auto best = std::max_element(pairCount.begin(), pairCount.end(),
                [](auto& a, auto& b) { return a.second < b.second; });

            if (best->second < 2) break; // No point replacing pairs that appear only once

            // Find a free byte
            bool used[256] = {};
            for (auto b : data) used[b] = true;
            int freeByte = -1;
            for (int i = 255; i >= 0; i--) {
                if (!used[i]) { freeByte = i; break; }
            }
            if (freeByte < 0) break; // No free bytes

            replacements.push_back({{best->first.first, best->first.second},
                                    (uint8_t)freeByte});

            // Replace all occurrences
            std::vector<uint8_t> newData;
            newData.reserve(data.size());
            for (size_t i = 0; i < data.size(); i++) {
                if (i + 1 < data.size() &&
                    data[i] == best->first.first &&
                    data[i+1] == best->first.second) {
                    newData.push_back((uint8_t)freeByte);
                    i++; // skip the second byte of the pair
                } else {
                    newData.push_back(data[i]);
                }
            }
            data = newData;
        }

        // Write replacement table
        uint16_t repCount = (uint16_t)replacements.size();
        output.push_back(repCount & 0xFF);
        output.push_back((repCount >> 8) & 0xFF);

        for (auto& r : replacements) {
            output.push_back(r.pair[0]);
            output.push_back(r.pair[1]);
            output.push_back(r.replacement);
        }

        // Write compressed data
        output.insert(output.end(), data.begin(), data.end());

        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        if (input.size() < 5) return {};

        size_t pos = 0;
        uint32_t origSize;
        memcpy(&origSize, input.data(), 4); pos += 4;

        int maxRounds = input[pos++];

        uint16_t repCount = input[pos] | (input[pos+1] << 8); pos += 2;

        struct Replacement {
            uint8_t pair[2];
            uint8_t replacement;
        };
        std::vector<Replacement> replacements;
        for (int i = 0; i < repCount; i++) {
            replacements.push_back({{input[pos], input[pos+1]}, input[pos+2]});
            pos += 3;
        }

        std::vector<uint8_t> data;
        while (pos < input.size()) {
            data.push_back(input[pos++]);
        }

        // Apply replacements in reverse order
        for (int i = (int)replacements.size() - 1; i >= 0; i--) {
            auto& r = replacements[i];
            std::vector<uint8_t> newData;
            for (auto b : data) {
                if (b == r.replacement) {
                    newData.push_back(r.pair[0]);
                    newData.push_back(r.pair[1]);
                } else {
                    newData.push_back(b);
                }
            }
            data = newData;
        }

        data.resize(origSize);
        return data;
    }
};

} // namespace mc
