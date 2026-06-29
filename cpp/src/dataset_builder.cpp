#include "lob/dataset_builder.hpp"
#include <algorithm>
#include <cmath>

namespace lob {

std::string DatasetBuilder::csv_header() {
    return "seq,ts_us,best_bid,best_bid_qty,best_ask,best_ask_qty,"
           "midprice,spread,microprice,ofi,queue_imbalance,"
           "arrival_rate,cancel_rate,"
           "bid_slope,ask_slope,volatility,trade_intensity,buy_ratio,"
           "prediction\n";
}

std::string DatasetBuilder::csv_row(const MarketSnapshot& snap) {
    auto fmt = [](auto v) { return std::to_string(v); };
    auto fmt_d = [](double v) {
        std::string s = std::to_string(v);
        // Trim trailing zeros
        auto dot = s.find('.');
        if (dot != std::string::npos) {
            auto last = s.find_last_not_of('0');
            if (last > dot) {
                s.erase(last + 1);
            } else {
                s.erase(dot + 2); // keep at least .0
            }
        }
        return s;
    };

    std::string row;
    row += fmt(snap.seq) + ",";
    row += fmt(snap.ts_us) + ",";
    row += fmt(snap.best_bid) + ",";
    row += fmt(snap.best_bid_qty) + ",";
    row += fmt(snap.best_ask) + ",";
    row += fmt(snap.best_ask_qty) + ",";
    row += fmt_d(snap.midprice) + ",";
    row += fmt_d(snap.spread) + ",";
    row += fmt_d(snap.microprice) + ",";
    row += fmt(snap.ofi) + ",";
    row += fmt_d(snap.queue_imbalance) + ",";
    row += fmt_d(snap.arrival_rate) + ",";
    row += fmt_d(snap.cancel_rate) + ",";
    row += fmt_d(snap.bid_slope) + ",";
    row += fmt_d(snap.ask_slope) + ",";
    row += fmt_d(snap.volatility) + ",";
    row += fmt_d(snap.trade_intensity) + ",";
    row += fmt_d(snap.buy_ratio) + ",";
    if (std::isnan(snap.prediction)) {
        row += "\n";
    } else {
        row += fmt_d(snap.prediction) + "\n";
    }
    return row;
}

DatasetBuilder::DatasetBuilder(std::string_view path) {
    file_.open(std::string(path));
    if (!file_.is_open()) {
        throw std::runtime_error("Cannot open output file: " + std::string(path));
    }
}

void DatasetBuilder::write(const MarketSnapshot& snap) {
    if (!header_written_) {
        file_ << csv_header();
        header_written_ = true;
    }
    file_ << csv_row(snap);
}

void DatasetBuilder::write_all(const std::vector<MarketSnapshot>& snaps) {
    if (snaps.empty()) return;
    if (!header_written_) {
        file_ << csv_header();
        header_written_ = true;
    }
    for (const auto& s : snaps) {
        file_ << csv_row(s);
    }
}

void DatasetBuilder::close() {
    file_.close();
}

} // namespace lob
