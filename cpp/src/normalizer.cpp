#include "lob/normalizer.hpp"

namespace lob {

std::vector<BookMessage> Normalizer::process(const ParseResult& result) {
    std::vector<BookMessage> out;
    out.reserve(result.messages.size());

    for (auto msg : result.messages) {
        auto k = key(msg.side, msg.px);
        if (msg.type == MessageType::Modify) {
            if (!seen_.contains(k)) {
                msg.type = MessageType::Add;
                seen_.insert(k);
            }
        } else if (msg.type == MessageType::Delete) {
            seen_.erase(k);
        }
        out.push_back(msg);
    }

    return out;
}

std::vector<BookMessage> Normalizer::process_snapshot(const std::vector<BookMessage>& msgs) {
    seen_.clear();
    std::vector<BookMessage> out;
    out.reserve(msgs.size());

    for (auto msg : msgs) {
        auto k = key(msg.side, msg.px);
        seen_.insert(k);
        out.push_back(msg);
    }

    return out;
}

void Normalizer::reset() {
    seen_.clear();
}

} // namespace lob
