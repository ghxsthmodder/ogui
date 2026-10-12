#pragma once
#include <functional>
#include <string>

namespace ogui {

// ------------------------------------------------------------------
// Clipboard: process-wide text clipboard.
//
// The default keeps the text in memory, which is enough for tests and
// headless hosts. A windowing host installs platform handlers (Win32
// CF_UNICODETEXT, X11 CLIPBOARD, ...) with setHandlers(); after that
// reads and writes go to the system clipboard.
// ------------------------------------------------------------------
class Clipboard {
public:
    using Get = std::function<std::string()>;
    using Set = std::function<void(const std::string&)>;

    static Clipboard& instance();

    // Pass two empty functions to go back to the in-memory default.
    void setHandlers(Get get, Set set);
    void reset() { setHandlers(Get{}, Set{}); }

    std::string text() const;
    void setText(const std::string& text);

private:
    Clipboard() = default;

    Get m_get;
    Set m_set;
    std::string m_fallback;
};

} // namespace ogui
