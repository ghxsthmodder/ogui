// stb_image: STB_IMAGE_IMPLEMENTATION compiles the decoder into this TU, so
// hosts do not need a separate stb_image.c.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "ogui/imagemanager.hpp"
#include "ogui/assets.hpp"

namespace ogui {

ImageManager& ImageManager::instance() {
    static ImageManager s_instance;
    return s_instance;
}

const ImageData* ImageManager::load(const std::string& path) {
    auto it = m_cache.find(path);
    if (it != m_cache.end()) return it->second.get();

    std::shared_ptr<ImageData> img;
    int w = 0, h = 0, channels = 0;
    // No vertical flip: row 0 is the top edge, matching the font atlas UV
    // convention (v = 0 at the top of a glyph quad).
    stbi_uc* px = nullptr;
    // Decoded from the bytes, wherever they came from: stb's own file loader
    // would put a filesystem path back in front of an asset host.
    if (auto bytes = readFile(path); bytes && !bytes->empty())
        px = stbi_load_from_memory(bytes->data(), static_cast<int>(bytes->size()),
                                   &w, &h, &channels, 4);
    if (px) {
        img = std::make_shared<ImageData>();
        img->id = m_nextId++;
        img->w = w;
        img->h = h;
        img->rgba.assign(px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        stbi_image_free(px);
        m_slots.push_back(img);
    }
    m_cache[path] = img;   // cache failures too (nullptr)
    return img.get();
}

void ImageManager::forEach(const std::function<void(const ImageData&)>& fn) const {
    for (const auto& img : m_slots) {
        if (img) fn(*img);
    }
}

bool ImageManager::ownsSlot(int id) const {
    for (const auto& img : m_slots) {
        if (img && img->id == id) return true;
    }
    return false;
}

} // namespace ogui
