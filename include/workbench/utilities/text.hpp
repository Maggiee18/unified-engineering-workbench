// Small, locale-independent text helpers shared by the file parsers.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wb::util {

/// Strips ASCII whitespace (space, tab, CR, LF, VT, FF) from both ends.
std::string_view trim(std::string_view text) noexcept;

/// Splits on runs of ASCII whitespace; empty tokens are never produced.
std::vector<std::string_view> splitWhitespace(std::string_view text);

/// Splits on a single delimiter character, keeping empty fields.
std::vector<std::string_view> splitDelimited(std::string_view text, char delimiter);

/// Case-insensitive ASCII comparison.
bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept;

/// Parses a decimal floating-point number using the "C" locale rules
/// regardless of the user's Windows regional settings. A leading '+' is
/// accepted. Returns std::nullopt if the whole token is not a number.
/// NOTE: "nan"/"inf" parse successfully; callers must check std::isfinite.
std::optional<double> parseDouble(std::string_view token) noexcept;

/// Parses a signed 64-bit decimal integer (whole token). std::nullopt on failure.
std::optional<std::int64_t> parseInt64(std::string_view token) noexcept;

/// Calls `onLine(lineNumber, lineText)` for every line; handles LF and CRLF.
/// The callback's text excludes the line terminator.
template <typename Callback>
void forEachLine(std::string_view text, Callback&& onLine) {
    std::size_t lineNumber = 0;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        onLine(++lineNumber, line);
        start = end + 1;
    }
}

/// Reads a whole file into memory, refusing files larger than `maxBytes`.
/// Throws wb::ValidationError for unreadable or oversized files.
std::string readFileLimited(const std::string& path, std::uint64_t maxBytes);

}  // namespace wb::util
