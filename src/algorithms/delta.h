#pragma once
// ============================================================================
// Delta Encoding
// Encodes differences between consecutive bytes - great for audio/sensors
// ============================================================================

#include <vector>
#include <cstdint>
#include <cstring>

namespace mc {

class Delta {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input) {
        std::vector<uint8_t> output;
        if (input.empty()) return output;

        uint32_t origSize = (uint32_t)input.size();
        output.resize(4);
        memcpy(output.data(), &origSize, 4);

        if (input.empty()) return output;

        uint8_t prev = input[0];
        output.push_back(input[0]); // first byte is literal

        for (size_t i = 1; i < input.size(); i++) {
            int diff = (int)input[i] - (int)prev;
            // Encode as signed 8-bit difference
            output.push_back((uint8_t)(diff & 0xFF));
            prev = input[i];
        }

        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        if (input.size() < 4) return {};

        size_t pos = 0;
        uint32_t origSize;
        memcpy(&origSize, input.data(), 4); pos += 4;

        if (pos >= input.size()) return {};

        std::vector<uint8_t> output;
        output.reserve(origSize);

        uint8_t prev = input[pos++];
        output.push_back(prev);

        while (pos < input.size() && output.size() < origSize) {
            int diff = (int)(int8_t)input[pos++];
            prev = (uint8_t)((int)prev + diff);
            output.push_back(prev);
        }

        return output;
    }
};

// Multi-level delta encoding
class DeltaMulti {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input, int order = 2) {
        std::vector<uint8_t> output;
        if (input.empty()) return output;

        uint32_t origSize = (uint32_t)input.size();
        output.resize(4);
        memcpy(output.data(), &origSize, 4);

        output.push_back((uint8_t)order);

        // Compute deltas
        std::vector<uint8_t> current(input.begin(), input.end());
        for (int o = 0; o < order; o++) {
            std::vector<uint8_t> next(current.size());
            next[0] = current[0];
            for (size_t i = 1; i < current.size(); i++) {
                int diff = (int)current[i] - (int)current[i-1];
                next[i] = (uint8_t)(diff & 0xFF);
            }
            current = next;
        }

        // Store the first `order` values from the delta-transformed data as initial samples
        for (int i = 0; i < order && i < (int)current.size(); i++) {
            output.push_back(current[i]);
        }

        // Write the rest of the delta data
        for (size_t i = order; i < current.size(); i++) {
            output.push_back(current[i]);
        }

        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        if (input.size() < 5) return {};

        size_t pos = 0;
        uint32_t origSize;
        memcpy(&origSize, input.data(), 4); pos += 4;

        int order = input[pos++];

        // Read all delta-transformed data (initial samples + rest)
        std::vector<uint8_t> data;
        data.reserve(origSize);
        while (pos < input.size() && data.size() < origSize) {
            data.push_back(input[pos++]);
        }
        data.resize(origSize);

        // Integrate `order` times to recover original
        for (int o = 0; o < order; o++) {
            for (size_t i = 1; i < data.size(); i++) {
                data[i] = (uint8_t)((int)data[i] + (int)data[i-1]);
            }
        }

        return data;
    }
};

} // namespace mc
