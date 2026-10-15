#pragma once
// ============================================================================
// Huffman Coding
// Adaptive Huffman coding for entropy reduction
// ============================================================================

#include <vector>
#include <cstdint>
#include <queue>
#include <map>
#include <algorithm>
#include <cstring>

namespace mc {

class Huffman {
public:
    struct Node {
        uint32_t freq;
        uint16_t symbol;
        int left, right;
        bool isLeaf;
    };

    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input) {
        std::vector<uint8_t> output;
        if (input.empty()) return output;

        // Count frequencies
        uint32_t freq[256] = {};
        for (auto b : input) freq[b]++;

        // Build header: symbol table
        uint32_t origSize = (uint32_t)input.size();
        output.resize(4);
        memcpy(output.data(), &origSize, 4);

        // Count non-zero symbols
        int nonZero = 0;
        for (int i = 0; i < 256; i++) if (freq[i] > 0) nonZero++;

        // Write symbol count
        uint16_t symCount = (uint16_t)nonZero;
        output.push_back(symCount & 0xFF);
        output.push_back((symCount >> 8) & 0xFF);

        // Write frequency table
        for (int i = 0; i < 256; i++) {
            if (freq[i] > 0) {
                output.push_back((uint8_t)i);
                output.push_back((uint8_t)(freq[i] & 0xFF));
                output.push_back((uint8_t)((freq[i] >> 8) & 0xFF));
                output.push_back((uint8_t)((freq[i] >> 16) & 0xFF));
                output.push_back((uint8_t)((freq[i] >> 24) & 0xFF));
            }
        }

        // Build Huffman tree
        std::vector<Node> nodes;
        auto cmp = [&](int a, int b) { return nodes[a].freq > nodes[b].freq; };
        std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

        for (int i = 0; i < 256; i++) {
            if (freq[i] > 0) {
                nodes.push_back({freq[i], (uint16_t)i, -1, -1, true});
                pq.push((int)nodes.size() - 1);
            }
        }

        if (pq.size() == 1) {
            // Single symbol: create dummy parent
            int idx = pq.top(); pq.pop();
            nodes.push_back({nodes[idx].freq, 0, idx, -1, false});
            pq.push((int)nodes.size() - 1);
        }

        while (pq.size() > 1) {
            int a = pq.top(); pq.pop();
            int b = pq.top(); pq.pop();
            nodes.push_back({nodes[a].freq + nodes[b].freq, 0, a, b, false});
            pq.push((int)nodes.size() - 1);
        }

        int root = pq.top();

        // Build code table
        std::map<uint16_t, std::pair<int, int>> codes; // symbol -> (bits, length)
        buildCodes(nodes, root, 0, 0, codes);

        // Encode data
        uint32_t bitBuffer = 0;
        int bitCount = 0;
        std::vector<uint8_t> bitStream;

        for (auto b : input) {
            auto& [bits, len] = codes[b];
            for (int i = len - 1; i >= 0; --i) {
                bitBuffer = (bitBuffer << 1) | ((bits >> i) & 1);
                bitCount++;
                if (bitCount == 8) {
                    bitStream.push_back((uint8_t)bitBuffer);
                    bitBuffer = 0;
                    bitCount = 0;
                }
            }
        }
        if (bitCount > 0) {
            bitBuffer <<= (8 - bitCount);
            bitStream.push_back((uint8_t)bitBuffer);
        }

        // Write bitstream length
        uint32_t bsLen = (uint32_t)bitStream.size();
        output.push_back(bsLen & 0xFF);
        output.push_back((bsLen >> 8) & 0xFF);
        output.push_back((bsLen >> 16) & 0xFF);
        output.push_back((bsLen >> 24) & 0xFF);

        output.insert(output.end(), bitStream.begin(), bitStream.end());
        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        std::vector<uint8_t> output;
        if (input.size() < 4) return output;

        size_t pos = 0;
        uint32_t origSize;
        memcpy(&origSize, input.data(), 4); pos += 4;

        uint16_t symCount = input[pos] | (input[pos + 1] << 8); pos += 2;

        uint32_t freq[256] = {};
        for (int i = 0; i < symCount; i++) {
            uint8_t sym = input[pos++];
            freq[sym] = input[pos] | (input[pos+1] << 8) |
                        (input[pos+2] << 16) | (input[pos+3] << 24);
            pos += 4;
        }

        // Read bitstream length
        uint32_t bsLen = input[pos] | (input[pos+1] << 8) |
                         (input[pos+2] << 16) | (input[pos+3] << 24);
        pos += 4;

        // Rebuild tree
        std::vector<Node> nodes;
        auto cmp = [&](int a, int b) { return nodes[a].freq > nodes[b].freq; };
        std::priority_queue<int, std::vector<int>, decltype(cmp)> pq(cmp);

        for (int i = 0; i < 256; i++) {
            if (freq[i] > 0) {
                nodes.push_back({freq[i], (uint16_t)i, -1, -1, true});
                pq.push((int)nodes.size() - 1);
            }
        }

        if (pq.size() == 1) {
            int idx = pq.top(); pq.pop();
            nodes.push_back({nodes[idx].freq, 0, idx, -1, false});
            pq.push((int)nodes.size() - 1);
        }

        while (pq.size() > 1) {
            int a = pq.top(); pq.pop();
            int b = pq.top(); pq.pop();
            nodes.push_back({nodes[a].freq + nodes[b].freq, 0, a, b, false});
            pq.push((int)nodes.size() - 1);
        }

        int root = pq.top();

        // Decode bits
        output.reserve(origSize);
        int nodeIdx = root;
        for (uint32_t i = 0; i < bsLen && output.size() < origSize; i++) {
            uint8_t byte = input[pos + i];
            for (int bit = 7; bit >= 0 && output.size() < origSize; bit--) {
                int dir = (byte >> bit) & 1;
                if (dir == 0) nodeIdx = nodes[nodeIdx].left;
                else          nodeIdx = nodes[nodeIdx].right;

                if (nodes[nodeIdx].isLeaf) {
                    output.push_back((uint8_t)nodes[nodeIdx].symbol);
                    nodeIdx = root;
                }
            }
        }

        return output;
    }

private:
    static void buildCodes(const std::vector<Node>& nodes, int idx,
                           uint32_t code, int len,
                           std::map<uint16_t, std::pair<int,int>>& codes) {
        if (idx < 0) return;
        if (nodes[idx].isLeaf) {
            codes[nodes[idx].symbol] = {code, len};
            return;
        }
        buildCodes(nodes, nodes[idx].left,  (code << 1),     len + 1, codes);
        buildCodes(nodes, nodes[idx].right, (code << 1) | 1, len + 1, codes);
    }
};

} // namespace mc
