#pragma once
// ogui — ImageManager: decodes image files once and owns their RGBA data.
// Mirrors FontManager: widgets ask for a path during draw, and each backend
// registers the resulting slots with its own texture upload path.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ogui {

// A decoded RGBA8 image plus the texture slot used in DrawCmd.texId.
// Slot ids are positive (0 means untextured, negative ids are font atlases).
struct ImageData {
    int id = 0;                        // stable positive slot id
    int w = 0;
    int h = 0;
    std::vector<uint8_t> rgba;         // w*h*4 bytes, row 0 = top edge
};

class ImageManager {
public:
    static ImageManager& instance();

    // Decode (or reuse the cached copy of) the image at `path`. The path is
    // resolved against the process working directory. Returns nullptr when
    // the file is missing or undecodable; failures are cached too so the
    // lookup stays cheap while drawing.
    const ImageData* load(const std::string& path);

    // Visit every successfully decoded image, in load order. Backends use
    // this to (re-)register textures for the slots they render.
    void forEach(const std::function<void(const ImageData&)>& fn) const;

    // True when `id` is a slot handed out by load(). Backends use this to
    // tell an image slot apart from a raw texture id a host created itself.
    bool ownsSlot(int id) const;

private:
    ImageManager() = default;

    std::map<std::string, std::shared_ptr<ImageData>> m_cache;
    std::vector<std::shared_ptr<ImageData>> m_slots;
    int m_nextId = 1;
};

} // namespace ogui
