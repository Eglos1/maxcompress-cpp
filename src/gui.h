#pragma once
// ============================================================================
// MaxCompress GTK3 GUI
// ============================================================================

#include <gtk/gtk.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>

#include "engine.h"

namespace mc {

struct GUIState {
    GtkWidget* window;
    GtkWidget* fileLabel;
    GtkWidget* modeCombo;
    GtkWidget* compressBtn;
    GtkWidget* decompressBtn;
    GtkWidget* progressBar;
    GtkWidget* statusLabel;
    GtkWidget* detailLabel;
    GtkWidget* textView;
    GtkTextBuffer* textBuffer;

    std::string inputPath;
    std::string outputPath;
    std::atomic<bool> running{false};
    std::atomic<int> progressPct{0};
    std::string statusText;
    std::string detailText;
    std::string logText;
    std::mutex mu;

    Engine engine;
};

// Thread-safe idle callbacks
static gboolean idleUpdateUI(gpointer data) {
    auto* st = reinterpret_cast<GUIState*>(data);
    if (!st->textBuffer) return G_SOURCE_REMOVE;
    std::lock_guard<std::mutex> lock(st->mu);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(st->progressBar),
                                   st->progressPct / 100.0);
    gtk_label_set_text(GTK_LABEL(st->statusLabel), st->statusText.c_str());
    gtk_label_set_text(GTK_LABEL(st->detailLabel), st->detailText.c_str());
    gtk_text_buffer_set_text(st->textBuffer, st->logText.c_str(), -1);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(st->textBuffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(st->textView),
                                  &end, 0.0, FALSE, 0.0, 1.0);
    return G_SOURCE_REMOVE;
}

static gboolean idleEnableButtons(gpointer data) {
    auto* st = reinterpret_cast<GUIState*>(data);
    gtk_widget_set_sensitive(st->compressBtn, TRUE);
    gtk_widget_set_sensitive(st->decompressBtn, TRUE);
    g_idle_add(idleUpdateUI, st);
    return G_SOURCE_REMOVE;
}

static void scheduleUpdate(GUIState* st) {
    g_idle_add(idleUpdateUI, st);
}

static void appendLog(GUIState* st, const std::string& msg) {
    std::lock_guard<std::mutex> lock(st->mu);
    st->logText += msg + "\n";
}

// ============================================================================
// Compress action
// ============================================================================
static void onCompress(GtkWidget*, GUIState* st) {
    if (st->running) return;

    GtkFileChooserNative* chooser = gtk_file_chooser_native_new(
        "Select File to Compress", GTK_WINDOW(st->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*");
    gtk_file_filter_set_name(filter, "All Files");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        st->inputPath = filename;
        st->outputPath = std::string(filename) + ".mxcp";
        g_free(filename);

        gtk_label_set_text(GTK_LABEL(st->fileLabel),
                           ("File: " + st->inputPath).c_str());
        gtk_widget_set_sensitive(st->compressBtn, FALSE);
        gtk_widget_set_sensitive(st->decompressBtn, FALSE);
        st->running = TRUE;
        st->progressPct = 0;
        st->statusText = "Compressing...";
        st->detailText = "";
        st->logText = "";
        scheduleUpdate(st);

        int modeIdx = gtk_combo_box_get_active(GTK_COMBO_BOX(st->modeCombo));
        std::string mode = (modeIdx == 0) ? "quick" :
                           (modeIdx == 2) ? "max" : "full";

        std::string inputPath = st->inputPath;
        std::string outputPath = st->outputPath;

        std::thread([st, mode, inputPath, outputPath]() {
            FILE* f = fopen(inputPath.c_str(), "rb");
            if (!f) {
                st->statusText = "Error: Cannot open file";
                st->running = FALSE;
                scheduleUpdate(st);
                g_idle_add(idleEnableButtons, st);
                return;
            }
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::vector<uint8_t> data(fsize);
            fread(data.data(), 1, fsize, f);
            fclose(f);

            appendLog(st, "File: " + inputPath);
            appendLog(st, "Size: " + std::to_string(data.size()) + " bytes");
            appendLog(st, "Mode: " + mode);
            appendLog(st, "Building algorithm registry...");
            scheduleUpdate(st);

            auto& eng = st->engine;

            auto result = eng.compress(data, mode,
                [st](const ProgressInfo& info) {
                    st->progressPct = (int)(info.tested * 100 / info.total);

                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(1);
                    oss << "[" << info.tested << "/" << info.total << "] "
                        << (info.tested * 100.0 / info.total) << "%"
                        << " | best: " << info.bestSize << " bytes ("
                        << (info.bestRatio * 100) << "%)"
                        << " | " << std::setprecision(0) << info.rate << "/s"
                        << " | ETA: " << info.eta << "s";
                    st->statusText = oss.str();

                    if (info.newBest && info.bestSize < SIZE_MAX) {
                        std::ostringstream detail;
                        detail << "NEW BEST: " << info.bestSize << " bytes"
                               << " (" << (info.bestRatio * 100) << "%)"
                               << " via " << info.bestMethod;
                        appendLog(st, detail.str());
                    }
                    st->detailText = "Best: " + info.bestMethod;
                    scheduleUpdate(st);
                });

            appendLog(st, "");
            appendLog(st, "=== RESULTS ===");
            appendLog(st, "Pipelines tested: " + std::to_string(result.pipelinesTested));
            appendLog(st, "Best method: " + result.method);
            appendLog(st, "Original: " + std::to_string(result.originalSize) + " bytes");
            appendLog(st, "Compressed: " + std::to_string(result.compressedSize) + " bytes");
            double ratio = (result.originalSize > 0) ?
                (double)result.compressedSize / result.originalSize : 1.0;
            std::ostringstream ratioSS;
            ratioSS << std::fixed << std::setprecision(2) << (ratio * 100) << "%";
            appendLog(st, "Ratio: " + ratioSS.str());
            double saved = (1.0 - ratio) * 100;
            std::ostringstream savedSS;
            savedSS << std::fixed << std::setprecision(2) << saved << "%";
            appendLog(st, "Saved: " + savedSS.str());
            appendLog(st, "Time: " + std::to_string(result.elapsedSeconds) + "s");
            appendLog(st, "Output: " + outputPath);

            FILE* out = fopen(outputPath.c_str(), "wb");
            if (out) {
                fwrite(result.data.data(), 1, result.data.size(), out);
                fclose(out);
            }

            std::ostringstream finalSS;
            finalSS << std::fixed << std::setprecision(2);
            finalSS << "Done! " << result.compressedSize << " bytes"
                    << " (" << (ratio * 100) << "%) saved " << saved << "%"
                    << " in " << result.elapsedSeconds << "s";
            st->statusText = finalSS.str();
            st->progressPct = 100;
            st->running = FALSE;
            scheduleUpdate(st);
            g_idle_add(idleEnableButtons, st);
        }).detach();
    }

    g_object_unref(chooser);
}

// ============================================================================
// Decompress action
// ============================================================================
static void onDecompress(GtkWidget*, GUIState* st) {
    if (st->running) return;

    GtkFileChooserNative* chooser = gtk_file_chooser_native_new(
        "Select File to Decompress", GTK_WINDOW(st->window),
        GTK_FILE_CHOOSER_ACTION_OPEN, "_Open", "_Cancel");

    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.mxcp");
    gtk_file_filter_set_name(filter, "MaxCompress Files");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);

    GtkFileFilter* allFilter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(allFilter, "*");
    gtk_file_filter_set_name(allFilter, "All Files");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), allFilter);

    if (gtk_native_dialog_run(GTK_NATIVE_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(chooser));
        st->inputPath = filename;
        g_free(filename);

        st->outputPath = st->inputPath;
        if (st->outputPath.size() > 5 &&
            st->outputPath.substr(st->outputPath.size() - 5) == ".mxcp") {
            st->outputPath = st->outputPath.substr(0, st->outputPath.size() - 5);
        } else {
            st->outputPath += ".restored";
        }

        gtk_label_set_text(GTK_LABEL(st->fileLabel),
                           ("File: " + st->inputPath).c_str());
        gtk_widget_set_sensitive(st->compressBtn, FALSE);
        gtk_widget_set_sensitive(st->decompressBtn, FALSE);
        st->running = TRUE;
        st->progressPct = 50;
        st->statusText = "Decompressing...";
        st->logText = "";
        scheduleUpdate(st);

        std::string inputPath = st->inputPath;
        std::string outputPath = st->outputPath;

        std::thread([st, inputPath, outputPath]() {
            FILE* f = fopen(inputPath.c_str(), "rb");
            if (!f) {
                st->statusText = "Error: Cannot open file";
                st->running = FALSE;
                scheduleUpdate(st);
                g_idle_add(idleEnableButtons, st);
                return;
            }
            fseek(f, 0, SEEK_END);
            long fsize = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::vector<uint8_t> data(fsize);
            fread(data.data(), 1, fsize, f);
            fclose(f);

            appendLog(st, "Input: " + inputPath);
            appendLog(st, "Size: " + std::to_string(data.size()) + " bytes");
            scheduleUpdate(st);

            if (data.size() < 13 || data[0] != 'M' || data[1] != 'X' ||
                data[2] != 'C' || data[3] != 'P') {
                appendLog(st, "Error: Not a MaxCompress file");
                st->statusText = "Error: Not a MaxCompress file";
            } else {
                auto& eng = st->engine;
                size_t pos = 4;
                uint8_t version = data[pos++];

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
                    pos += 8; // skip original size
                    uint32_t compLen = data[pos] | (data[pos+1]<<8) |
                                       (data[pos+2]<<16) | (data[pos+3]<<24);
                    pos += 4;
                    std::vector<uint8_t> compData(data.begin() + pos, data.begin() + pos + compLen);

                    appendLog(st, "Pipeline: " + pipe.describe(eng.algos));
                    appendLog(st, "Payload: " + std::to_string(compData.size()) + " bytes");
                    scheduleUpdate(st);

                    auto result = eng.reversePipeline(compData, pipe);
                    if (!result.empty()) {
                        FILE* out = fopen(outputPath.c_str(), "wb");
                        if (out) { fwrite(result.data(), 1, result.size(), out); fclose(out); }
                        appendLog(st, "Output size: " + std::to_string(result.size()) + " bytes");
                        appendLog(st, "Saved to: " + outputPath);
                        st->statusText = "Decompressed successfully!";
                    } else {
                        st->statusText = "Error: Decompression failed";
                        appendLog(st, "Decompression failed");
                    }
                } else {
                    appendLog(st, "Unsupported version: " + std::to_string(version));
                    st->statusText = "Error: Unsupported version";
                }
            }

            st->progressPct = 100;
            st->running = FALSE;
            scheduleUpdate(st);
            g_idle_add(idleEnableButtons, st);
        }).detach();
    }

    g_object_unref(chooser);
}

// ============================================================================
// Build GUI
// ============================================================================
static void buildGUI(GUIState* st) {
    st->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(st->window), "MaxCompress - Ultimate Compression Engine");
    gtk_window_set_default_size(GTK_WINDOW(st->window), 720, 620);
    g_signal_connect(st->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    // Apply monospace CSS
    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        ".mono-text view { font-family: monospace; font-size: 10pt; }", -1, NULL);
    GtkStyleContext* ctx = gtk_style_context_new();
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    g_object_unref(ctx);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_container_add(GTK_CONTAINER(st->window), vbox);

    // Title
    GtkWidget* title = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(title),
        "<span size='xx-large' weight='bold'>MaxCompress</span>\n"
        "<span size='small'>Tries hundreds of algorithm combinations for maximum compression</span>");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 4);

    gtk_box_pack_start(GTK_BOX(vbox),
        gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 0);

    // File label
    st->fileLabel = gtk_label_new("No file selected");
    gtk_label_set_xalign(GTK_LABEL(st->fileLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), st->fileLabel, FALSE, FALSE, 2);

    // Controls row
    GtkWidget* ctrlBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), ctrlBox, FALSE, FALSE, 4);

    st->compressBtn = gtk_button_new_with_label("Compress");
    g_signal_connect(st->compressBtn, "clicked", G_CALLBACK(onCompress), st);
    gtk_widget_set_size_request(st->compressBtn, 120, -1);
    gtk_box_pack_start(GTK_BOX(ctrlBox), st->compressBtn, FALSE, FALSE, 0);

    st->decompressBtn = gtk_button_new_with_label("Decompress");
    g_signal_connect(st->decompressBtn, "clicked", G_CALLBACK(onDecompress), st);
    gtk_widget_set_size_request(st->decompressBtn, 120, -1);
    gtk_box_pack_start(GTK_BOX(ctrlBox), st->decompressBtn, FALSE, FALSE, 0);

    GtkWidget* modeLabel = gtk_label_new("Mode:");
    gtk_box_pack_start(GTK_BOX(ctrlBox), modeLabel, FALSE, FALSE, 8);

    st->modeCombo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st->modeCombo), "Quick (~500)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st->modeCombo), "Full (~2000+)");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(st->modeCombo), "Max (~5000+)");
    gtk_combo_box_set_active(GTK_COMBO_BOX(st->modeCombo), 1);
    gtk_box_pack_start(GTK_BOX(ctrlBox), st->modeCombo, FALSE, FALSE, 0);

    // Progress bar
    st->progressBar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(vbox), st->progressBar, FALSE, FALSE, 4);

    // Status
    st->statusLabel = gtk_label_new("Ready");
    gtk_label_set_xalign(GTK_LABEL(st->statusLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), st->statusLabel, FALSE, FALSE, 2);

    st->detailLabel = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(st->detailLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), st->detailLabel, FALSE, FALSE, 2);

    // Log text view
    GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 4);

    st->textBuffer = gtk_text_buffer_new(NULL);
    st->textView = gtk_text_view_new_with_buffer(st->textBuffer);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(st->textView), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(st->textView), TRUE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(st->textView), 8);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(st->textView), 8);
    GtkStyleContext* tvCtx = gtk_widget_get_style_context(st->textView);
    gtk_style_context_add_class(tvCtx, "mono-text");
    gtk_container_add(GTK_CONTAINER(scroll), st->textView);

    // Algorithm info
    GtkWidget* algoInfo = gtk_label_new(NULL);
    std::string infoText = "<span size='small'>Algorithms: ";
    auto& algos = st->engine.algos;
    for (size_t i = 0; i < algos.size(); i++) {
        if (i > 0) infoText += ", ";
        infoText += algos[i].name;
    }
    infoText += "</span>";
    gtk_label_set_markup(GTK_LABEL(algoInfo), infoText.c_str());
    gtk_label_set_line_wrap(GTK_LABEL(algoInfo), TRUE);
    gtk_label_set_xalign(GTK_LABEL(algoInfo), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), algoInfo, FALSE, FALSE, 2);

    gtk_widget_show_all(st->window);
}

} // namespace mc
