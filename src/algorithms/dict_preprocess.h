#pragma once
// ============================================================================
// Dictionary-Based Preprocessor
// Finds repeated patterns and replaces them with tokens
// ============================================================================

#include <vector>
#include <cstdint>
#include <map>
#include <algorithm>
#include <cstring>

namespace mc {

class DictPreprocessor {
public:
    struct Params {
        int minPatternLen = 3;
        int maxPatternLen = 64;
        int minOccurrences = 2;
        int maxEntries = 4096;
    };

    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input,
                                         const Params& p) {
        std::vector<uint8_t> output;
        if (input.size() < (size_t)(p.minPatternLen * p.minOccurrences)) {
            uint32_t zero = 0;
            output.resize(4);
            memcpy(output.data(), &zero, 4);
            output.insert(output.end(), input.begin(), input.end());
            return output;
        }

        // Count pattern occurrences
        std::map<std::vector<uint8_t>, int> counts;
        int step = std::max(1, (int)input.size() / 4000);
        for (int wsize = p.minPatternLen;
             wsize <= std::min(p.maxPatternLen, (int)input.size() / 2);
             wsize++) {
            for (size_t i = 0; i + wsize <= input.size(); i += step) {
                std::vector<uint8_t> pat(input.begin() + i,
                                         input.begin() + i + wsize);
                counts[pat]++;
            }
        }

        // Score and rank
        struct ScoredPattern {
            int savings;
            std::vector<uint8_t> pattern;
            int count;
        };
        std::vector<ScoredPattern> scored;
        for (auto& [pat, cnt] : counts) {
            if (cnt < p.minOccurrences) continue;
            int savings = ((int)pat.size() - 3) * (cnt - 1) - 3;
            if (savings > 0) {
                scored.push_back({savings, pat, cnt});
            }
        }
        std::sort(scored.begin(), scored.end(),
            [](const ScoredPattern& a, const ScoredPattern& b) {
                if (a.savings != b.savings) return a.savings > b.savings;
                return a.pattern.size() > b.pattern.size();
            });
        if ((int)scored.size() > p.maxEntries)
            scored.resize(p.maxEntries);

        if (scored.empty()) {
            uint32_t zero = 0;
            output.resize(4);
            memcpy(output.data(), &zero, 4);
            output.insert(output.end(), input.begin(), input.end());
            return output;
        }

        // Build dictionary sorted longest-first
        std::vector<std::vector<uint8_t>> dictionary;
        for (auto& sp : scored) {
            dictionary.push_back(sp.pattern);
        }
        std::sort(dictionary.begin(), dictionary.end(),
            [](const auto& a, const auto& b) { return a.size() > b.size(); });

        // Build lookup: first byte -> list of (pattern, id)
        std::map<uint8_t, std::vector<std::pair<std::vector<uint8_t>, int>>> byFirst;
        for (int i = 0; i < (int)dictionary.size(); i++) {
            byFirst[dictionary[i][0]].push_back({dictionary[i], i});
        }

        // Write header
        uint32_t numEntries = (uint32_t)dictionary.size();
        output.resize(4);
        memcpy(output.data(), &numEntries, 4);

        // Write dictionary
        for (auto& pat : dictionary) {
            uint16_t plen = (uint16_t)pat.size();
            output.push_back(plen & 0xFF);
            output.push_back((plen >> 8) & 0xFF);
            output.insert(output.end(), pat.begin(), pat.end());
        }

        // Replace patterns
        std::vector<uint8_t> processed;
        size_t i = 0;
        while (i < input.size()) {
            uint8_t fb = input[i];
            auto it = byFirst.find(fb);
            bool matched = false;
            if (it != byFirst.end()) {
                int bestId = -1;
                int bestLen = 0;
                for (auto& [pat, pid] : it->second) {
                    int plen = (int)pat.size();
                    if (plen > bestLen &&
                        i + plen <= input.size() &&
                        memcmp(input.data() + i, pat.data(), plen) == 0) {
                        bestId = pid;
                        bestLen = plen;
                    }
                }
                if (bestId >= 0) {
                    processed.push_back(0xFF);
                    processed.push_back((bestId >> 8) & 0xFF);
                    processed.push_back(bestId & 0xFF);
                    i += bestLen;
                    matched = true;
                }
            }
            if (!matched) {
                if (fb == 0xFF) {
                    processed.push_back(0xFF);
                    processed.push_back(0xFF);
                } else {
                    processed.push_back(fb);
                }
                i++;
            }
        }

        // Write processed data length
        uint32_t dataLen = (uint32_t)processed.size();
        uint8_t dlBytes[4];
        memcpy(dlBytes, &dataLen, 4);
        output.insert(output.end(), dlBytes, dlBytes + 4);
        output.insert(output.end(), processed.begin(), processed.end());

        return output;
    }

    // Default-params overload
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input) {
        return compress(input, Params());
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        if (input.size() < 4) return {};

        size_t pos = 0;
        uint32_t numEntries;
        memcpy(&numEntries, input.data() + pos, 4); pos += 4;

        std::vector<std::vector<uint8_t>> dictionary;
        for (uint32_t i = 0; i < numEntries; i++) {
            uint16_t plen = input[pos] | (input[pos+1] << 8); pos += 2;
            dictionary.push_back(std::vector<uint8_t>(
                input.begin() + pos, input.begin() + pos + plen));
            pos += plen;
        }

        if (pos + 4 > input.size()) return {};
        uint32_t dataLen;
        memcpy(&dataLen, input.data() + pos, 4); pos += 4;

        std::vector<uint8_t> output;
        output.reserve(dataLen * 2); // decoded can be larger than encoded

        // Decode all remaining bytes
        while (pos < input.size()) {
            if (input[pos] == 0xFF) {
                pos++;
                if (pos >= input.size()) break;
                if (input[pos] == 0xFF) {
                    output.push_back(0xFF);
                    pos++;
                } else {
                    int tid = (input[pos] << 8) | input[pos+1];
                    pos += 2;
                    if (tid < (int)dictionary.size()) {
                        output.insert(output.end(),
                                      dictionary[tid].begin(),
                                      dictionary[tid].end());
                    }
                }
            } else {
                output.push_back(input[pos]);
                pos++;
            }
        }

        return output;
    }
};

} // namespace mc
