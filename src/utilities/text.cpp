#include "workbench/utilities/text.hpp"

#include "workbench/core/errors.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace wb::util {
namespace {

constexpr bool isSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}

constexpr char toLowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
    return text;
}

std::vector<std::string_view> splitWhitespace(std::string_view text) {
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSpace(text[i])) ++i;
        const std::size_t begin = i;
        while (i < text.size() && !isSpace(text[i])) ++i;
        if (i > begin) tokens.push_back(text.substr(begin, i - begin));
    }
    return tokens;
}

std::vector<std::string_view> splitDelimited(std::string_view text, char delimiter) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(delimiter, start);
        if (end == std::string_view::npos) {
            fields.push_back(text.substr(start));
            break;
        }
        fields.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return fields;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (toLowerAscii(a[i]) != toLowerAscii(b[i])) return false;
    }
    return true;
}

std::optional<double> parseDouble(std::string_view token) noexcept {
    if (!token.empty() && token.front() == '+') token.remove_prefix(1);
    if (token.empty()) return std::nullopt;
    double value = 0.0;
    // std::from_chars is locale-independent: "1.5" parses identically on a
    // German or Indian Windows locale, unlike strtod/stod.
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc() || ptr != token.data() + token.size()) return std::nullopt;
    return value;
}

std::optional<std::int64_t> parseInt64(std::string_view token) noexcept {
    if (!token.empty() && token.front() == '+') token.remove_prefix(1);
    if (token.empty()) return std::nullopt;
    std::int64_t value = 0;
    const auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec != std::errc() || ptr != token.data() + token.size()) return std::nullopt;
    return value;
}

std::string readFileLimited(const std::string& path, std::uint64_t maxBytes) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p = fs::u8path(path);
    if (!fs::is_regular_file(p, ec)) {
        throw ValidationError("file not found or not a regular file: " + path);
    }
    const std::uint64_t size = fs::file_size(p, ec);
    if (ec) throw ValidationError("cannot determine size of file: " + path);
    if (size > maxBytes) {
        throw ValidationError("file is " + std::to_string(size) + " bytes; the limit is " +
                              std::to_string(maxBytes) + " bytes");
    }
    std::ifstream in(p, std::ios::binary);
    if (!in) throw ValidationError("cannot open file for reading: " + path);
    std::string content(static_cast<std::size_t>(size), '\0');
    if (size > 0 && !in.read(content.data(), static_cast<std::streamsize>(size))) {
        throw ValidationError("failed while reading file: " + path);
    }
    return content;
}

}  // namespace wb::util
