// ============================================================================
// MaxCompress - Ultimate Multi-Algorithm Compression Engine
// Pure C++ with GTK3 GUI
// Tries hundreds of algorithm combinations to find optimal compression
// ============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <iostream>

#include "engine.h"
#include "gui.h"

// ============================================================================
// CLI mode
// ============================================================================

static void printUsage() {
    std::cout <<
        "MaxCompress - Ultimate Multi-Algorithm Compression Engine\n"
        "=========================================================\n\n"
        "Usage:\n"
        "  maxcompress compress <input> [-o output] [-m mode]  Compress a file\n"
        "  maxcompress decompress <input> [-o output]           Decompress a file\n"
        "  maxcompress info                                     Show algorithms\n"
        "  maxcompress gui                                      Launch GUI\n\n"
        "Modes: quick (~500), full (~2000+), max (~5000+)\n"
        "Default: full\n\n"
        "The engine tries every combination of its built-in algorithms:\n"
        "  LZ77 (multiple window sizes), Huffman, BWT+MTF+RLE,\n"
        "  LZW (multiple bit widths), RLE, Delta encoding,\n"
        "  Byte-Pair Encoding (multiple rounds), Dictionary preprocessor,\n"
        "  plus multi-pass variants of each.\n\n"
        "All algorithms are implemented directly in C++ - no external\n"
        "compression libraries needed.\n";
}

static std::vector<uint8_t> readFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { std::cerr << "Error: Cannot open " << path << "\n"; exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> data(sz);
    fread(data.data(), 1, sz, f);
    fclose(f);
    return data;
}

static void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { std::cerr << "Error: Cannot write " << path << "\n"; exit(1); }
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

// MXCP file format v4: stores full pipeline description
static std::vector<uint8_t> packMXCP(const std::vector<uint8_t>& compData,
                                       const mc::Pipeline& pipe,
                                       const mc::Engine& engine,
                                       size_t origSize) {
    std::vector<uint8_t> out;
    // Magic
    out.push_back('M'); out.push_back('X'); out.push_back('C'); out.push_back('P');
    // Version
    out.push_back(4);

    // Encode pipeline as JSON-like metadata
    // Format: numSteps(1) [algoIdx(2)]... useDict(1) dictPasses(1)
    //         origSize(8) compLen(4) compData...
    out.push_back((uint8_t)pipe.algoIndices.size());
    for (auto idx : pipe.algoIndices) {
        out.push_back((idx >> 8) & 0xFF);
        out.push_back(idx & 0xFF);
    }
    out.push_back(pipe.useDict ? 1 : 0);
    out.push_back((uint8_t)pipe.dictPasses);

    // Original size (8 bytes)
    out.push_back(origSize & 0xFF);
    out.push_back((origSize >> 8) & 0xFF);
    out.push_back((origSize >> 16) & 0xFF);
    out.push_back((origSize >> 24) & 0xFF);
    out.push_back((origSize >> 32) & 0xFF);
    out.push_back((origSize >> 40) & 0xFF);
    out.push_back((origSize >> 48) & 0xFF);
    out.push_back((origSize >> 56) & 0xFF);

    // Compressed data size (4 bytes)
    uint32_t compLen = (uint32_t)compData.size();
    out.push_back(compLen & 0xFF);
    out.push_back((compLen >> 8) & 0xFF);
    out.push_back((compLen >> 16) & 0xFF);
    out.push_back((compLen >> 24) & 0xFF);

    // Compressed data
    out.insert(out.end(), compData.begin(), compData.end());
    return out;
}

static int cliCompress(const std::string& input, const std::string& output,
                        const std::string& mode) {
    std::cout << "[MaxCompress] Compressing: " << input << "\n";
    auto data = readFile(input);
    std::cout << "[MaxCompress] Size: " << data.size() << " bytes\n";

    mc::Engine engine;
    auto result = engine.compress(data, mode,
        [](const mc::ProgressInfo& info) {
            std::cout << "\r  [" << info.tested << "/" << info.total << "] "
                      << std::fixed << std::setprecision(1)
                      << (info.tested * 100.0 / info.total) << "%"
                      << " | best: " << info.bestSize << " bytes ("
                      << std::setprecision(1) << (info.bestRatio * 100) << "%)"
                      << " | " << std::setprecision(0) << info.rate << "/s"
                      << " | ETA: " << std::setprecision(0) << info.eta << "s"
                      << "   " << std::flush;
        });
    std::cout << "\n";

    auto packed = packMXCP(result.data, result.pipeline, engine, result.originalSize);
    writeFile(output, packed);

    double ratio = (result.originalSize > 0) ?
        (double)result.compressedSize / result.originalSize : 1.0;
    std::cout << "\n  Method:   " << result.method << "\n"
              << "  Tested:   " << result.pipelinesTested << " combinations\n"
              << "  Original: " << result.originalSize << " bytes\n"
              << "  Compressed: " << result.compressedSize << " bytes\n"
              << "  Ratio:    " << std::fixed << std::setprecision(2)
              << (ratio * 100) << "%\n"
              << "  Saved:    " << ((1.0 - ratio) * 100) << "%\n"
              << "  Time:     " << std::setprecision(1)
              << result.elapsedSeconds << "s\n"
              << "  Output:   " << output << "\n";

    return 0;
}

static int cliDecompress(const std::string& input, const std::string& output) {
    std::cout << "[MaxCompress] Decompressing: " << input << "\n";
    auto data = readFile(input);

    if (data.size() < 13 || data[0] != 'M' || data[1] != 'X' ||
        data[2] != 'C' || data[3] != 'P') {
        std::cerr << "Error: Not a MaxCompress file\n";
        return 1;
    }

    size_t pos = 4;
    uint8_t version = data[pos++];

    mc::Engine engine;

    if (version >= 4) {
        // v4: binary pipeline encoding
        uint8_t numSteps = data[pos++];
        mc::Pipeline pipe;
        pipe.algoIndices.resize(numSteps);
        for (int i = 0; i < numSteps; i++) {
            pipe.algoIndices[i] = (data[pos] << 8) | data[pos+1];
            pos += 2;
        }
        pipe.useDict = data[pos++] != 0;
        pipe.dictPasses = data[pos++];

        uint64_t origSize = 0;
        memcpy(&origSize, data.data() + pos, 8); pos += 8;
        uint32_t compLen = data[pos] | (data[pos+1]<<8) |
                           (data[pos+2]<<16) | (data[pos+3]<<24);
        pos += 4;
        std::vector<uint8_t> compData(data.begin() + pos, data.begin() + pos + compLen);

        std::cout << "[MaxCompress] Pipeline: " << pipe.describe(engine.algos) << "\n";

        auto result = engine.reversePipeline(compData, pipe);
        if (!result.empty()) {
            writeFile(output, result);
            std::cout << "[MaxCompress] Decompressed to: " << output
                      << " (" << result.size() << " bytes)\n";
            return 0;
        } else {
            std::cerr << "[MaxCompress] Error: Decompression failed\n";
            return 1;
        }
    } else {
        // v3: method string
        uint32_t metaLen = data[pos] | (data[pos+1]<<8) |
                           (data[pos+2]<<16) | (data[pos+3]<<24);
        pos += 4;
        std::string method(data.begin() + pos, data.begin() + pos + metaLen);
        pos += metaLen;
        pos += 8; // skip original size
        uint32_t compLen = data[pos] | (data[pos+1]<<8) |
                           (data[pos+2]<<16) | (data[pos+3]<<24);
        pos += 4;
        std::vector<uint8_t> compData(data.begin() + pos, data.begin() + pos + compLen);

        std::cout << "[MaxCompress] Method (v3): " << method << "\n";

        auto pipelines = engine.buildPipelines("full");
        for (auto& pipe : pipelines) {
            if (pipe.describe(engine.algos) != method) continue;
            try {
                auto result = engine.reversePipeline(compData, pipe);
                if (!result.empty()) {
                    writeFile(output, result);
                    std::cout << "[MaxCompress] Decompressed to: " << output
                              << " (" << result.size() << " bytes)\n";
                    return 0;
                }
            } catch (...) {}
        }
        std::cerr << "[MaxCompress] Error: Could not decompress\n";
        return 1;
    }
}

static void showInfo() {
    mc::Engine engine;
    std::cout << "MaxCompress Algorithm Registry\n"
              << "==============================\n\n";
    std::cout << "Total algorithms: " << engine.algos.size() << "\n\n";

    for (size_t i = 0; i < engine.algos.size(); i++) {
        std::cout << "  [" << i << "] " << engine.algos[i].name << "\n";
    }

    std::cout << "\nPipeline counts:\n";
    for (auto& mode : {"quick", "full", "max"}) {
        auto pipes = engine.buildPipelines(mode);
        std::cout << "  " << mode << ": " << pipes.size() << " combinations\n";
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        // Launch GUI by default
        mc::GUIState st;
        mc::buildGUI(&st);
        gtk_main();
        return 0;
    }

    std::string cmd = argv[1];

    if (cmd == "gui") {
        mc::GUIState st;
        mc::buildGUI(&st);
        gtk_main();
        return 0;
    }

    if (cmd == "info") {
        showInfo();
        return 0;
    }

    if (cmd == "compress") {
        if (argc < 3) { printUsage(); return 1; }
        std::string input = argv[2];
        std::string output = input + ".mxcp";
        std::string mode = "full";

        for (int i = 3; i < argc; i++) {
            if ((std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output") && i+1 < argc)
                output = argv[++i];
            else if (std::string(argv[i]) == "-m" || std::string(argv[i]) == "--mode")
                mode = argv[++i];
            else if (std::string(argv[i]) == "-q" || std::string(argv[i]) == "--quick")
                mode = "quick";
            else if (std::string(argv[i]) == "--max")
                mode = "max";
        }

        return cliCompress(input, output, mode);
    }

    if (cmd == "decompress") {
        if (argc < 3) { printUsage(); return 1; }
        std::string input = argv[2];
        std::string output;

        for (int i = 3; i < argc; i++) {
            if ((std::string(argv[i]) == "-o" || std::string(argv[i]) == "--output") && i+1 < argc)
                output = argv[++i];
        }

        if (output.empty()) {
            output = input;
            if (output.size() > 5 && output.substr(output.size()-5) == ".mxcp")
                output = output.substr(0, output.size() - 5);
            else
                output += ".restored";
        }

        return cliDecompress(input, output);
    }

    printUsage();
    return 1;
}
