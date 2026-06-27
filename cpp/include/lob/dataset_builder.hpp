#pragma once

#include "lob/snapshot.hpp"
#include <string>
#include <string_view>
#include <fstream>
#include <vector>

namespace lob {

class DatasetBuilder {
public:
    explicit DatasetBuilder(std::string_view path);

    void write(const MarketSnapshot& snap);
    void write_all(const std::vector<MarketSnapshot>& snaps);

    void close();
    bool is_open() const noexcept { return file_.is_open(); }

private:
    std::ofstream file_;
    bool header_written_ = false;

    static std::string csv_header();
    static std::string csv_row(const MarketSnapshot& snap);
};

} // namespace lob
