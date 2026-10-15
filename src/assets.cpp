#include "ogui/assets.hpp"

#include <fstream>
#include <utility>

namespace ogui {

namespace {
AssetReader g_reader;
}

void setAssetReader(AssetReader reader) { g_reader = std::move(reader); }

const AssetReader& assetReader() { return g_reader; }

std::optional<std::vector<uint8_t>> readFile(const std::string& path) {
    if (g_reader) {
        if (auto owned = g_reader(path)) return owned;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>());
}

}  // namespace ogui
