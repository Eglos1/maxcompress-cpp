#pragma once
// ============================================================================
// Burrows-Wheeler Transform + Move-to-Front + Run-Length Encoding
// Classic bzip2-style compression chain
// ============================================================================

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace mc {

class BWT {
public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input) {
        if (input.empty()) return {};
        if (input.size() == 1) {
            return {0, 0, 0, 0, input[0]};
        }

        // Find BWT using sorting
        size_t n = input.size();

        // Header: original size
        std::vector<uint8_t> output;
        uint32_t origSize = (uint32_t)n;
        output.resize(4);
        memcpy(output.data(), &origSize, 4);

        // Create rotation indices
        std::vector<uint32_t> indices(n);
        std::iota(indices.begin(), indices.end(), 0);

        // Sort rotations
        std::sort(indices.begin(), indices.end(),
            [&input, n](uint32_t a, uint32_t b) {
                for (size_t i = 0; i < n; i++) {
                    uint8_t ca = input[(a + i) % n];
                    uint8_t cb = input[(b + i) % n];
                    if (ca != cb) return ca < cb;
                }
                return false;
            });

        // Find primary index and write BWT output
        uint32_t primaryIdx = 0;
        std::vector<uint8_t> bwt(n);
        for (size_t i = 0; i < n; i++) {
            bwt[i] = input[(indices[i] + n - 1) % n];
            if (indices[i] == 0) primaryIdx = (uint32_t)i;
        }

        // Write primary index
        output.push_back(primaryIdx & 0xFF);
        output.push_back((primaryIdx >> 8) & 0xFF);
        output.push_back((primaryIdx >> 16) & 0xFF);
        output.push_back((primaryIdx >> 24) & 0xFF);

        // Apply Move-to-Front transform
        std::vector<uint8_t> mtf(256);
        std::iota(mtf.begin(), mtf.end(), 0);

        std::vector<uint8_t> mtfOutput(n);
        for (size_t i = 0; i < n; i++) {
            uint8_t val = bwt[i];
            // Find position in MTF list
            int pos = 0;
            while (mtf[pos] != val) pos++;
            mtfOutput[i] = (uint8_t)pos;
            // Move to front
            uint8_t c = mtf[pos];
            mtf.erase(mtf.begin() + pos);
            mtf.insert(mtf.begin(), c);
        }

        // Run-length encode the MTF output (zero runs)
        // Encoding scheme:
        //   0x00-0xEF: literal value as-is
        //   0xF0: escape marker — next byte is a literal (for values 0xF0-0xFF)
        //   0xF1-0xFF: zero run of (byte & 0x0F) zeros (1..15)
        std::vector<uint8_t> rle;
        int runLen = 0;
        for (size_t i = 0; i < n; i++) {
            if (mtfOutput[i] == 0) {
                runLen++;
            } else {
                // Encode any pending zero run
                while (runLen > 0) {
                    int chunk = std::min(runLen, 15);
                    rle.push_back((uint8_t)(chunk | 0xF0));
                    runLen -= chunk;
                }
                // Encode literal
                if (mtfOutput[i] >= 0xF0) {
                    rle.push_back(0xF0); // escape marker
                }
                rle.push_back(mtfOutput[i]);
            }
        }
        // Flush remaining zeros
        while (runLen > 0) {
            int chunk = std::min(runLen, 15);
            rle.push_back((uint8_t)(chunk | 0xF0));
            runLen -= chunk;
        }

        // Write RLE length
        uint32_t rleLen = (uint32_t)rle.size();
        output.push_back(rleLen & 0xFF);
        output.push_back((rleLen >> 8) & 0xFF);
        output.push_back((rleLen >> 16) & 0xFF);
        output.push_back((rleLen >> 24) & 0xFF);

        output.insert(output.end(), rle.begin(), rle.end());
        return output;
    }

    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input) {
        if (input.size() < 12) return {};

        size_t pos = 0;
        uint32_t origSize;
        memcpy(&origSize, input.data(), 4); pos += 4;

        if (origSize == 0 || origSize > 100 * 1024 * 1024) return {}; // sanity

        uint32_t primaryIdx = input[pos] | (input[pos+1] << 8) |
                             (input[pos+2] << 16) | (input[pos+3] << 24);
        pos += 4;

        if (primaryIdx >= origSize) return {}; // invalid

        uint32_t rleLen = input[pos] | (input[pos+1] << 8) |
                          (input[pos+2] << 16) | (input[pos+3] << 24);
        pos += 4;

        // Decode RLE -> MTF
        // Encoding scheme:
        //   0x00-0xEF: literal value as-is
        //   0xF0: escape marker — next byte is a literal (for values 0xF0-0xFF)
        //   0xF1-0xFF: zero run of (byte & 0x0F) zeros (1..15)
        std::vector<uint8_t> mtf;
        mtf.reserve(origSize);
        for (uint32_t i = 0; i < rleLen && mtf.size() < origSize; ) {
            if (pos + i >= input.size()) break;
            uint8_t b = input[pos + i];
            i++;
            if (b >= 0xF1) {
                // Zero run: 1..15 zeros
                int zeros = b & 0x0F;
                for (int z = 0; z < zeros && mtf.size() < origSize; z++) {
                    mtf.push_back(0);
                }
            } else if (b == 0xF0) {
                // Escape: next byte is a literal
                if (pos + i < input.size() && mtf.size() < origSize) {
                    mtf.push_back(input[pos + i]);
                    i++;
                }
            } else {
                // Literal 0x00-0xEF
                mtf.push_back(b);
            }
        }

        // Reverse MTF
        std::vector<uint8_t> mtfTable(256);
        std::iota(mtfTable.begin(), mtfTable.end(), 0);

        if (mtf.size() != origSize) return {}; // size mismatch

        std::vector<uint8_t> bwt(origSize);
        for (size_t i = 0; i < origSize; i++) {
            uint8_t pos2 = mtf[i];
            bwt[i] = mtfTable[pos2];
            uint8_t c = mtfTable[pos2];
            mtfTable.erase(mtfTable.begin() + pos2);
            mtfTable.insert(mtfTable.begin(), c);
        }

        // Reverse BWT using LF-mapping
        // Build cumulative counts: C[c] = number of characters less than c in bwt
        std::vector<uint32_t> count(256, 0);
        for (size_t i = 0; i < origSize; i++) {
            count[bwt[i]]++;
        }

        uint32_t C[256];
        C[0] = 0;
        for (int i = 1; i < 256; i++) {
            C[i] = C[i-1] + count[i-1];
        }

        // Build LF mapping: LF[i] = C[bwt[i]] + rank(bwt[i], i)
        // This maps a row in the last column to the corresponding row in the first column
        std::vector<uint32_t> LF(origSize);
        std::vector<uint32_t> rank(256, 0);
        for (size_t i = 0; i < origSize; i++) {
            LF[i] = C[bwt[i]] + rank[bwt[i]]++;
        }

        // Reconstruct original by following the LF chain backwards from primaryIdx
        std::vector<uint8_t> output(origSize);
        uint32_t idx = primaryIdx;
        for (int i = (int)origSize - 1; i >= 0; i--) {
            output[i] = bwt[idx];
            idx = LF[idx];
        }

        return output;
    }
};

} // namespace mc
