#include "workbench/thermal/nodal_csv.hpp"

#include "workbench/core/errors.hpp"
#include "workbench/utilities/text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

namespace wb::thermal {
namespace {

constexpr std::array<const char*, 5> kRequiredColumns = {"node_id", "x_m", "y_m", "z_m", "temperature_C"};

/// Removes optional surrounding double quotes and whitespace from a field.
std::string_view cleanField(std::string_view field) {
    field = util::trim(field);
    if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
        field = util::trim(field.substr(1, field.size() - 2));
    }
    return field;
}

double parseFiniteField(std::string_view field, std::size_t line, const char* column) {
    const auto value = util::parseDouble(field);
    if (!value) {
        throw ParseError(line, std::string("column '") + column + "' value '" + std::string(field) +
                                   "' is not a number");
    }
    if (!std::isfinite(*value)) {
        throw ParseError(line, std::string("column '") + column + "' value '" + std::string(field) +
                                   "' is not finite (NaN/Inf are rejected)");
    }
    return *value;
}

class NodalCsvParser {
public:
    NodalCsvParser(std::string_view text, const NodalCsvLimits& limits) : text_(text), limits_(limits) {}

    NodalDataset run() {
        if (text_.empty()) throw ParseError(0, "the CSV file is empty (0 bytes)");
        if (text_.size() > limits_.maxBytes) {
            throw ParseError(0, "CSV text is " + std::to_string(text_.size()) + " bytes; the limit is " +
                                    std::to_string(limits_.maxBytes));
        }
        // Excel and some exporters write a UTF-8 byte-order mark.
        if (text_.size() >= 3 && static_cast<unsigned char>(text_[0]) == 0xEF &&
            static_cast<unsigned char>(text_[1]) == 0xBB && static_cast<unsigned char>(text_[2]) == 0xBF) {
            text_.remove_prefix(3);
        }
        if (util::trim(text_).empty()) {
            throw ParseError(0, "the CSV file contains only whitespace - no header or data rows");
        }
        util::forEachLine(text_, [this](std::size_t n, std::string_view line) { parseLine(n, line); });

        if (!headerSeen_) {
            throw ParseError(0, "no header row found; expected: node_id,x_m,y_m,z_m,temperature_C");
        }
        if (data_.nodes.empty()) {
            throw ParseError(0, "the CSV has a valid header but no data rows");
        }
        computeRanges();
        return std::move(data_);
    }

private:
    void parseLine(std::size_t n, std::string_view raw) {
        const std::string_view line = util::trim(raw);
        if (line.empty()) return;
        if (line.front() == '#') {
            data_.comments.emplace_back(util::trim(line.substr(1)));
            return;
        }
        const auto fields = util::splitDelimited(line, ',');
        if (!headerSeen_) {
            parseHeader(n, fields);
            return;
        }
        if (fields.size() != headerWidth_) {
            throw ParseError(n, "expected " + std::to_string(headerWidth_) + " fields, found " +
                                    std::to_string(fields.size()));
        }
        if (data_.nodes.size() >= limits_.maxNodes) {
            throw ParseError(n, "node limit of " + std::to_string(limits_.maxNodes) + " exceeded");
        }
        NodalTemperature node;
        const std::string_view idField = cleanField(fields[columnIndex_[0]]);
        const auto id = util::parseInt64(idField);
        if (!id) throw ParseError(n, "node_id '" + std::string(idField) + "' is not an integer");
        if (*id <= 0) throw ParseError(n, "node_id must be a positive integer");
        node.nodeId = *id;
        node.x = parseFiniteField(cleanField(fields[columnIndex_[1]]), n, "x_m");
        node.y = parseFiniteField(cleanField(fields[columnIndex_[2]]), n, "y_m");
        node.z = parseFiniteField(cleanField(fields[columnIndex_[3]]), n, "z_m");
        node.temperatureC = parseFiniteField(cleanField(fields[columnIndex_[4]]), n, "temperature_C");
        if (node.temperatureC < kAbsoluteZeroC) {
            throw ParseError(n, "temperature_C " + std::to_string(node.temperatureC) +
                                    " is below absolute zero (-273.15 C)");
        }
        if (!seenIds_.insert(node.nodeId).second) {
            throw ParseError(n, "duplicate node_id " + std::to_string(node.nodeId));
        }
        data_.nodes.push_back(node);
    }

    void parseHeader(std::size_t n, const std::vector<std::string_view>& fields) {
        columnIndex_.fill(kMissing);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const std::string_view name = cleanField(fields[i]);
            bool known = false;
            for (std::size_t c = 0; c < kRequiredColumns.size(); ++c) {
                if (util::equalsIgnoreCase(name, kRequiredColumns[c])) {
                    if (columnIndex_[c] != kMissing) {
                        throw ParseError(n, "duplicate header column '" + std::string(name) + "'");
                    }
                    columnIndex_[c] = i;
                    known = true;
                }
            }
            if (!known) data_.warnings.push_back("extra column '" + std::string(name) + "' ignored");
        }
        std::string missing;
        for (std::size_t c = 0; c < kRequiredColumns.size(); ++c) {
            if (columnIndex_[c] == kMissing) {
                if (!missing.empty()) missing += ", ";
                missing += kRequiredColumns[c];
            }
        }
        if (!missing.empty()) {
            throw ParseError(n, "header is missing required column(s): " + missing +
                                    " (expected node_id,x_m,y_m,z_m,temperature_C)");
        }
        headerWidth_ = fields.size();
        headerSeen_ = true;
    }

    void computeRanges() {
        const auto& first = data_.nodes.front();
        data_.minTemperatureC = data_.maxTemperatureC = first.temperatureC;
        double lo[3] = {first.x, first.y, first.z};
        double hi[3] = {first.x, first.y, first.z};
        for (const auto& node : data_.nodes) {
            data_.minTemperatureC = std::min(data_.minTemperatureC, node.temperatureC);
            data_.maxTemperatureC = std::max(data_.maxTemperatureC, node.temperatureC);
            const double p[3] = {node.x, node.y, node.z};
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], p[a]);
                hi[a] = std::max(hi[a], p[a]);
            }
        }
        std::copy(lo, lo + 3, data_.minCoord);
        std::copy(hi, hi + 3, data_.maxCoord);
    }

    static constexpr std::size_t kMissing = static_cast<std::size_t>(-1);

    std::string_view text_;
    NodalCsvLimits limits_;
    NodalDataset data_;
    bool headerSeen_ = false;
    std::size_t headerWidth_ = 0;
    std::array<std::size_t, 5> columnIndex_{};
    std::unordered_set<std::int64_t> seenIds_;
};

}  // namespace

NodalDataset parseNodalTemperatureCsv(std::string_view text, const NodalCsvLimits& limits) {
    return NodalCsvParser(text, limits).run();
}

NodalDataset loadNodalTemperatureCsvFile(const std::string& path, const NodalCsvLimits& limits) {
    const std::string content = util::readFileLimited(path, limits.maxBytes);
    return parseNodalTemperatureCsv(content, limits);
}

}  // namespace wb::thermal
