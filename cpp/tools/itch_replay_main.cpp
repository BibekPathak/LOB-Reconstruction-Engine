#include <lob/itch_parser.hpp>
#include <lob/reconstructor.hpp>
#include <lob/feature_engine.hpp>
#include <lob/snapshot.hpp>
#include <lob/dataset_builder.hpp>
#include <lob/predictor.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <vector>

using namespace lob;


static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s --input <itch_binary> [--model <model.txt>] [--output <out.csv>]\n", prog);
    fprintf(stderr, "\nIf --output is omitted, CSV is written to stdout.\n");
    exit(1);
}

int main(int argc, char** argv) {
    std::string input_path, model_path, output_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) input_path = argv[++i];
        else if (arg == "--model" && i + 1 < argc) model_path = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_path = argv[++i];
        else usage(argv[0]);
    }

    if (input_path.empty()) usage(argv[0]);

    // ── Load ITCH binary file ─────────────────────────────────────────────
    FILE* f = fopen(input_path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", input_path.c_str());
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    rewind(f);

    std::vector<uint8_t> buf(static_cast<size_t>(file_size));
    if (fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        fprintf(stderr, "Error: read failed\n");
        fclose(f);
        return 1;
    }
    fclose(f);
    fprintf(stderr, "Loaded %zu bytes from %s\n", buf.size(), input_path.c_str());

    // ── Pipeline ──────────────────────────────────────────────────────────
    ItchParser parser;
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;

    // Optional predictor
    Predictor pred;
    if (!model_path.empty()) {
        if (pred.load_model(model_path)) {
            se.set_predictor(&pred);
            fprintf(stderr, "Predictor loaded: %s\n", model_path.c_str());
        } else {
            fprintf(stderr, "Warning: could not load model %s\n", model_path.c_str());
        }
    }

    // ── Parse and process ───────────────────────────────────────────────
    auto result = parser.parse(buf.data(), buf.size());
    fprintf(stderr, "Parsed %zu messages (track %u .. %u)\n",
            result.messages.size(), result.first_track, result.last_track);

    if (parser.has_gap()) {
        auto g = parser.last_gap();
        fprintf(stderr, "Sequence gap: expected=%u actual=%u\n", g.expected, g.actual);
    }

    for (const auto& msg : result.messages) {
        rec.apply(msg);
        fe.update(msg, rec);
        se.check(rec, fe, msg.ts_us);
    }

    fprintf(stderr, "Captured %zu snapshots\n", se.data().size());

    // ── Write output ─────────────────────────────────────────────────────
    if (output_path.empty()) {
        printf("%s", DatasetBuilder::csv_header().c_str());
        for (const auto& s : se.data()) {
            printf("%s", DatasetBuilder::csv_row(s).c_str());
        }
        fflush(stdout);
    } else {
        DatasetBuilder db(output_path);
        db.write_all(se.data());
        db.close();
        fprintf(stderr, "CSV written to %s\n", output_path.c_str());
    }

    return 0;
}
