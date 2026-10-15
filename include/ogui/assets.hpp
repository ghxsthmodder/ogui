#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ogui {

// Bytes of a named resource, or nullopt when this reader does not have it.
using AssetReader =
    std::function<std::optional<std::vector<uint8_t>>(const std::string&)>;

// Where the library reads the bytes it opens from. Nothing inside ogui calls
// fopen any more: layouts, themes, scripts, fonts and images all come through
// readFile(), which asks this reader first and only then the filesystem. A
// normal host installs nothing and keeps reading files as it always did; an
// Android host points it at AAsset, a web host at its VFS, a test at a map of
// strings, and none of them has to ship the layout directory next to the
// binary. Returning nullopt means "not mine", so a reader that only owns some
// names can let the rest fall through to disk.
void setAssetReader(AssetReader reader);
const AssetReader& assetReader();

// The single entry point every loader uses: the installed reader first, then
// the filesystem. nullopt is "cannot open"; an existing but empty resource is
// an empty vector, because a missing layout and a corrupt one are different
// errors to report.
std::optional<std::vector<uint8_t>> readFile(const std::string& path);

}  // namespace ogui
