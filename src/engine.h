#pragma once
// ============================================================================
// MaxCompress C++ Engine
// Tries hundreds/thousands of algorithm combinations to find optimal
// compression pipeline
// ============================================================================

#include <vector>
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <mutex>
#include <atomic>
#include <set>
#include <chrono>

#include "algorithms/lz77.h"
#include "algorithms/huffman.h"
#include "algorithms/bwt.h"
#include "algorithms/lzw.h"
#include "algorithms/rle.h"
#include "algorithms/delta.h"
#include "algorithms/bpe.h"
#include "algorithms/dict_preprocess.h"

namespace mc {

// ============================================================================
// Algorithm Registry
// ============================================================================

struct AlgorithmInfo {
    std::string name;
    std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)> compress;
    std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)> decompress;
    std::vector<int> levels; // compression levels (0 means no level param)
};

inline std::vector<AlgorithmInfo> buildAlgorithmRegistry() {
    std::vector<AlgorithmInfo> algos;

    // --- LZ77 with different window sizes ---
    for (int winPow : {10, 12, 14, 15}) {
        int winSize = 1 << winPow;
        int maxMatch = std::min(258, winSize);
        std::string name = "lz77_w" + std::to_string(winSize);
        mc::LZ77Params params;
        params.windowSize = winSize;
        params.maxMatchLen = maxMatch;
        algos.push_back({name,
            [params](const std::vector<uint8_t>& d) {
                return mc::LZ77::compress(d, params);
            },
            [](const std::vector<uint8_t>& d) { return mc::LZ77::decompress(d); },
            {1}});
    }

    // --- Huffman ---
    algos.push_back({"huffman",
        [](const std::vector<uint8_t>& d) { return mc::Huffman::compress(d); },
        [](const std::vector<uint8_t>& d) { return mc::Huffman::decompress(d); },
        {1}});

    // --- BWT + MTF + RLE ---
    algos.push_back({"bwt",
        [](const std::vector<uint8_t>& d) { return mc::BWT::compress(d); },
        [](const std::vector<uint8_t>& d) { return mc::BWT::decompress(d); },
        {1}});

    // --- LZW with different max bits ---
    for (int bits : {10, 11, 12, 13, 14}) {
        std::string name = "lzw_b" + std::to_string(bits);
        algos.push_back({name,
            [bits](const std::vector<uint8_t>& d) {
                return mc::LZW::compress(d, bits);
            },
            [](const std::vector<uint8_t>& d) { return mc::LZW::decompress(d); },
            {1}});
    }

    // --- RLE ---
    algos.push_back({"rle",
        [](const std::vector<uint8_t>& d) { return mc::RLE::compress(d); },
        [](const std::vector<uint8_t>& d) { return mc::RLE::decompress(d); },
        {1}});

    // --- Delta encoding ---
    algos.push_back({"delta",
        [](const std::vector<uint8_t>& d) { return mc::Delta::compress(d); },
        [](const std::vector<uint8_t>& d) { return mc::Delta::decompress(d); },
        {1}});

    // --- Multi-level Delta ---
    for (int order : {2, 3, 4}) {
        std::string name = "delta_m" + std::to_string(order);
        algos.push_back({name,
            [order](const std::vector<uint8_t>& d) {
                return mc::DeltaMulti::compress(d, order);
            },
            [](const std::vector<uint8_t>& d) { return mc::DeltaMulti::decompress(d); },
            {1}});
    }

    // --- BPE (different rounds) ---
    for (int rounds : {3, 5, 8, 12}) {
        std::string name = "bpe_r" + std::to_string(rounds);
        algos.push_back({name,
            [rounds](const std::vector<uint8_t>& d) {
                return mc::BPE::compress(d, rounds);
            },
            [](const std::vector<uint8_t>& d) { return mc::BPE::decompress(d); },
            {1}});
    }

    // --- Multi-pass LZ77 ---
    for (int pass : {2, 3}) {
        mc::LZ77Params params;
        params.windowSize = 32768;
        std::string name = "lz77_x" + std::to_string(pass);
        algos.push_back({name,
            [pass, params](const std::vector<uint8_t>& d) {
                auto data = d;
                for (int i = 0; i < pass; i++)
                    data = mc::LZ77::compress(data, params);
                return data;
            },
            [pass](const std::vector<uint8_t>& d) {
                auto data = d;
                for (int i = 0; i < pass; i++)
                    data = mc::LZ77::decompress(data);
                return data;
            },
            {1}});
    }

    // --- Multi-pass Huffman ---
    for (int pass : {2, 3}) {
        std::string name = "huffman_x" + std::to_string(pass);
        algos.push_back({name,
            [pass](const std::vector<uint8_t>& d) {
                auto data = d;
                for (int i = 0; i < pass; i++)
                    data = mc::Huffman::compress(data);
                return data;
            },
            [pass](const std::vector<uint8_t>& d) {
                auto data = d;
                for (int i = 0; i < pass; i++)
                    data = mc::Huffman::decompress(data);
                return data;
            },
            {1}});
    }

    // --- Multi-pass BPE ---
    for (int pass : {2}) {
        std::string name = "bpe_x" + std::to_string(pass);
        algos.push_back({name,
            [pass](const std::vector<uint8_t>& d) {
                auto data = d;
                for (int i = 0; i < pass; i++)
                    data = mc::BPE::compress(data, 5);
                return data;
            },
            [pass](const std::vector<uint8_t>& d) {
                auto data = d;
                for (int i = 0; i < pass; i++)
                    data = mc::BPE::decompress(data);
                return data;
            },
            {1}});
    }

    return algos;
}

// ============================================================================
// Pipeline Definition
// ============================================================================

struct PipelineStep {
    int algoIndex;
};

struct Pipeline {
    std::vector<int> algoIndices;
    bool useDict = false;
    int dictPasses = 1;

    std::string describe(const std::vector<AlgorithmInfo>& algos) const {
        std::string s;
        if (useDict) {
            s += "dict(x" + std::to_string(dictPasses) + ")";
        }
        for (auto idx : algoIndices) {
            if (!s.empty()) s += " -> ";
            s += algos[idx].name;
        }
        if (s.empty()) s = "raw";
        return s;
    }
};

// ============================================================================
// Progress Callback
// ============================================================================

struct ProgressInfo {
    int total;
    int tested;
    int failed;
    size_t bestSize;
    double bestRatio;
    std::string bestMethod;
    bool newBest;
    double elapsed;
    double rate;
    double eta;
};

using ProgressCallback = std::function<void(const ProgressInfo&)>;

// ============================================================================
// Engine
// ============================================================================

class Engine {
public:
    std::vector<AlgorithmInfo> algos;
    DictPreprocessor dict;

    Engine() {
        algos = buildAlgorithmRegistry();
    }

    std::vector<Pipeline> buildPipelines(const std::string& mode = "full") const {
        std::vector<Pipeline> pipes;
        int n = (int)algos.size();

        // Helper: index of algorithm by name
        auto findAlgo = [&](const std::string& name) -> int {
            for (int i = 0; i < (int)algos.size(); i++)
                if (algos[i].name == name) return i;
            return -1;
        };

        // ------------------------------------------------------------------
        // PASS 1: Single algorithms (all of them)
        // ------------------------------------------------------------------
        for (int i = 0; i < n; i++) {
            pipes.push_back({{i}, false, 1});
            pipes.push_back({{i}, true, 1});
            if (mode != "quick") {
                pipes.push_back({{i}, true, 2});
            }
            if (mode == "max") {
                pipes.push_back({{i}, true, 3});
            }
        }

        // ------------------------------------------------------------------
        // PASS 2: 2-algorithm chains at best settings
        // ------------------------------------------------------------------
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                if (i == j) continue;
                pipes.push_back({{i, j}, false, 1});
                pipes.push_back({{i, j}, true, 1});
            }
        }

        // ------------------------------------------------------------------
        // PASS 3: 3-algorithm chains (limited set)
        // ------------------------------------------------------------------
        int chain3Limit = (mode == "quick") ? std::min(6, n) :
                          (mode == "max")   ? std::min(10, n) :
                                              std::min(8, n);
        for (int i = 0; i < chain3Limit; i++) {
            for (int j = 0; j < chain3Limit; j++) {
                if (j == i) continue;
                for (int k = 0; k < chain3Limit; k++) {
                    if (k == i || k == j) continue;
                    pipes.push_back({{i, j, k}, false, 1});
                    pipes.push_back({{i, j, k}, true, 1});
                }
            }
        }

        // ------------------------------------------------------------------
        // PASS 4: 4-algorithm chains (max mode only)
        // ------------------------------------------------------------------
        if (mode == "max") {
            int chain4Limit = std::min(6, n);
            for (int i = 0; i < chain4Limit; i++) {
                for (int j = 0; j < chain4Limit; j++) {
                    if (j == i) continue;
                    for (int k = 0; k < chain4Limit; k++) {
                        if (k == i || k == j) continue;
                        for (int l = 0; l < chain4Limit; l++) {
                            if (l == i || l == j || l == k) continue;
                            pipes.push_back({{i, j, k, l}, false, 1});
                            if (mode == "max") {
                                pipes.push_back({{i, j, k, l}, true, 1});
                            }
                        }
                    }
                }
            }
        }

        // ------------------------------------------------------------------
        // PASS 5: Same-algorithm pairs (lz77 -> lz77, bpe -> bpe, etc.)
        // ------------------------------------------------------------------
        for (int i = 0; i < n; i++) {
            std::string name = algos[i].name;
            if (name.find("lz77") != std::string::npos ||
                name.find("bpe") != std::string::npos ||
                name.find("huffman") != std::string::npos ||
                name.find("lzw") != std::string::npos) {
                pipes.push_back({{i, i}, false, 1});
                pipes.push_back({{i, i}, true, 1});
            }
        }

        // ------------------------------------------------------------------
        // PASS 6: Smart curated combos
        // ------------------------------------------------------------------
        struct SmartCombo {
            std::vector<std::string> algoNames;
            bool withDict;
            int dictPasses;
            SmartCombo(std::initializer_list<std::string> a, bool d = false, int dp = 1)
                : algoNames(a), withDict(d), dictPasses(dp) {}
        };
        std::vector<SmartCombo> smart = {
            // BWT-based chains
            {"bwt", "huffman"},
            {"bwt", "lz77_w32768"},
            {"bwt", "huffman", "lz77_w32768"},
            // LZ77 combos
            {"lz77_w32768", "huffman"},
            {"lz77_w16384", "huffman"},
            {"lz77_w32768", "bpe_r5"},
            {"lz77_w16384", "bwt"},
            {"lz77_w4096", "huffman"},
            // BPE chains
            {"bpe_r8", "huffman"},
            {"bpe_r5", "lz77_w32768"},
            {"bpe_r12", "huffman"},
            {"bpe_r8", "bwt"},
            {"bpe_r5", "bwt", "huffman"},
            {"bpe_r8", "bwt", "huffman"},
            // LZW chains
            {"lzw_b12", "huffman"},
            {"lzw_b14", "huffman"},
            {"lzw_b12", "bwt"},
            {"lzw_b12", "bpe_r5"},
            // Delta chains
            {"delta", "huffman"},
            {"delta_m2", "huffman"},
            {"delta_m3", "huffman"},
            {"delta", "lz77_w32768"},
            {"delta_m2", "bwt"},
            {"delta", "bwt", "huffman"},
            // Triple chains
            {"bwt", "huffman", "lz77_w32768"},
            {"bpe_r5", "bwt", "huffman"},
            {"lzw_b12", "bwt", "huffman"},
            {"lz77_w32768", "bwt", "huffman"},
            {"delta_m2", "bwt", "huffman"},
            {"bpe_r8", "bwt", "lz77_w32768"},
            // Multi-pass chains
            {"lz77_x2", "huffman"},
            {"lz77_x3", "huffman"},
            {"huffman_x2", "lz77_w32768"},
            {"bpe_x2", "huffman"},
            {"bpe_x2", "bwt"},
            // Deep chains (4-way)
            {"bpe_r5", "bwt", "huffman", "lz77_w32768"},
            {"bpe_r8", "bwt", "huffman", "lz77_w16384"},
            {"lzw_b12", "bwt", "huffman", "lz77_w32768"},
            {"delta_m2", "bwt", "huffman", "lz77_w32768"},
            {"lz77_w32768", "bwt", "huffman", "lz77_w16384"},
            {"bpe_r5", "bwt", "huffman", "lzw_b12"},
        };

        for (auto& sc : smart) {
            std::vector<int> indices;
            bool valid = true;
            for (auto& name : sc.algoNames) {
                int idx = findAlgo(name);
                if (idx < 0) { valid = false; break; }
                indices.push_back(idx);
            }
            if (!valid || indices.empty()) continue;

            pipes.push_back({indices, false, 1});
            pipes.push_back({indices, true, 1});
            if (mode == "max") {
                pipes.push_back({indices, true, 2});
            }
        }

        // De-duplicate
        std::set<std::string> seen;
        std::vector<Pipeline> unique;
        for (auto& p : pipes) {
            std::string key;
            for (auto i : p.algoIndices) key += std::to_string(i) + ":";
            key += std::to_string(p.useDict ? 1 : 0) + ":" +
                   std::to_string(p.dictPasses);
            if (seen.insert(key).second) {
                unique.push_back(p);
            }
        }

        return unique;
    }

    // Apply pipeline forward (compress)
    std::vector<uint8_t> applyPipeline(const std::vector<uint8_t>& data,
                                        const Pipeline& pipe) const {
        std::vector<uint8_t> current = data;

        if (pipe.useDict) {
            for (int i = 0; i < pipe.dictPasses; i++) {
                current = mc::DictPreprocessor::compress(current);
                if (current.size() > data.size() * 2) return {}; // abort if exploding
            }
        }

        for (auto idx : pipe.algoIndices) {
            auto prev = current;
            current = algos[idx].compress(current);
            // If compression made it bigger, abort this pipeline
            if (current.size() > prev.size() * 1.5 && prev.size() > 1024) {
                return {};
            }
        }

        return current;
    }

    // Apply pipeline backward (decompress)
    std::vector<uint8_t> reversePipeline(const std::vector<uint8_t>& data,
                                          const Pipeline& pipe) const {
        std::vector<uint8_t> current = data;

        for (int i = (int)pipe.algoIndices.size() - 1; i >= 0; i--) {
            size_t prevSize = current.size();
            current = algos[pipe.algoIndices[i]].decompress(current);
            // Sanity check: decompressed should be >= compressed and not explode
            if (current.empty() || current.size() > prevSize * 1000) {
                return {};
            }
        }

        if (pipe.useDict) {
            for (int i = 0; i < pipe.dictPasses; i++) {
                current = mc::DictPreprocessor::decompress(current);
            }
        }

        return current;
    }

    // Result struct
    struct CompressResult {
        std::vector<uint8_t> data;
        Pipeline pipeline;
        size_t originalSize;
        size_t compressedSize;
        int pipelinesTested;
        double elapsedSeconds;
        std::string method;
    };

    // Main compress function
    CompressResult compress(const std::vector<uint8_t>& data,
                            const std::string& mode = "full",
                            ProgressCallback progress = nullptr) {
        auto t0 = std::chrono::steady_clock::now();
        size_t origSize = data.size();

        auto pipelines = buildPipelines(mode);
        int total = (int)pipelines.size();

        std::vector<uint8_t> bestData;
        Pipeline bestPipeline;
        size_t bestSize = SIZE_MAX;
        int tested = 0;
        int failed = 0;

        for (auto& pipe : pipelines) {
            tested++;

            auto result = applyPipeline(data, pipe);
            bool newBest = false;

            if (!result.empty() && result.size() < bestSize) {
                bestSize = result.size();
                bestData = result;
                bestPipeline = pipe;
                newBest = true;
            } else if (result.empty()) {
                failed++;
            }

            // Progress callback
            if (progress && tested % 50 == 0) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - t0).count();
                double rate = tested / elapsed;
                double eta = (total - tested) / rate;

                ProgressInfo info;
                info.total = total;
                info.tested = tested;
                info.failed = failed;
                info.bestSize = bestSize;
                info.bestRatio = (origSize > 0) ? (double)bestSize / origSize : 1.0;
                info.bestMethod = bestPipeline.describe(algos);
                info.newBest = newBest;
                info.elapsed = elapsed;
                info.rate = rate;
                info.eta = eta;
                progress(info);
            }
        }

        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();

        if (bestData.empty()) {
            bestPipeline = Pipeline{{}, false, 1};
            bestData = data;
            bestSize = data.size();
        }

        return {bestData, bestPipeline, origSize, bestSize, tested, elapsed,
                bestPipeline.describe(algos)};
    }
};

} // namespace mc
