#include <lob/binance_parser.hpp>
#include <lob/normalizer.hpp>
#include <lob/reconstructor.hpp>
#include <lob/feature_engine.hpp>
#include <lob/snapshot.hpp>
#include <lob/dataset_builder.hpp>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace lob;


static void usage(const char* prog) {
    fprintf(stderr, "Usage: %s --input <raw_messages.jsonl> [--output <out.csv>]\n", prog);
    fprintf(stderr, "\nIf --output is omitted, CSV is written to stdout.\n");
    exit(1);
}

int main(int argc, char** argv) {
    std::string input_path, output_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) input_path = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_path = argv[++i];
        else usage(argv[0]);
    }

    if (input_path.empty()) usage(argv[0]);

    FILE* f = fopen(input_path.c_str(), "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", input_path.c_str());
        return 1;
    }

    std::vector<std::string> lines;
    char* buf = nullptr;
    size_t buf_len = 0;
    ssize_t nread;
    while ((nread = getline(&buf, &buf_len, f)) != -1) {
        std::string line(buf, nread);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
            line.pop_back();
        if (!line.empty()) lines.push_back(std::move(line));
    }
    free(buf);
    fclose(f);
    fprintf(stderr, "Loaded %zu lines from %s\n", lines.size(), input_path.c_str());

    // ── Pipeline ─────────────────────────────────────────────────────────
    Normalizer norm;
    Reconstructor rec;
    FeatureEngine fe;
    SnapshotEngine se;

    for (const auto& line : lines) {
        if (line.find("\"snapshot\"") != std::string::npos) {
            auto msgs = BinanceParser::parse_snapshot(line);
            for (const auto& m : norm.process_snapshot(msgs)) {
                rec.apply(m);
                fe.update(m, rec);
                se.check(rec, fe, m.ts_us);
            }
        } else if (line.find("depthUpdate") != std::string::npos) {
            auto result = BinanceParser::parse_depth_update(line);
            for (const auto& m : norm.process(result)) {
                rec.apply(m);
                fe.update(m, rec);
                se.check(rec, fe, m.ts_us);
            }
        }
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
