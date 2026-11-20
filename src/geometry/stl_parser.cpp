#include "workbench/core/errors.hpp"
#include "workbench/geometry/stl_mesh.hpp"
#include "workbench/utilities/text.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace wb::geometry {
namespace {

using util::equalsIgnoreCase;

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

/// Binary STL has an 80-byte header that may itself start with "solid", so the
/// keyword alone is not proof of ASCII. Control bytes in the first 512 bytes are.
bool looksBinary(std::string_view text) {
    const std::size_t probe = std::min<std::size_t>(text.size(), 512);
    for (std::size_t i = 0; i < probe; ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == 0 || (c < 0x20 && c != '\t' && c != '\n' && c != '\r' && c != '\v' && c != '\f')) {
            return true;
        }
    }
    return false;
}

/// States of the facet grammar. Each keyword is only legal in one state.
enum class State { ExpectSolid, ExpectFacetOrEnd, ExpectOuterLoop, ExpectVertex, ExpectEndLoop,
                   ExpectEndFacet, Done };

class StlParser {
public:
    StlParser(std::string_view text, const StlLimits& limits) : text_(text), limits_(limits) {}

    StlMesh run() {
        if (text_.empty()) throw ParseError(0, "the STL file is empty (0 bytes)");
        if (text_.size() > limits_.maxBytes) {
            throw ParseError(0, "STL text is " + std::to_string(text_.size()) +
                                    " bytes; the limit is " + std::to_string(limits_.maxBytes));
        }
        if (util::trim(text_).empty()) {
            throw ParseError(0, "the STL file contains only whitespace - no 'solid' block found");
        }
        if (looksBinary(text_)) {
            throw ParseError(0, "file looks like binary STL; only ASCII STL is supported");
        }

        util::forEachLine(text_, [this](std::size_t n, std::string_view line) { parseLine(n, line); });

        if (state_ != State::Done) {
            throw ParseError(lastLine_, "unexpected end of file (" + describeExpectation() + ")");
        }
        if (mesh_.triangles.empty()) {
            throw ParseError(0, "the STL solid contains no facets");
        }
        finalise();
        return std::move(mesh_);
    }

private:
    void parseLine(std::size_t n, std::string_view line) {
        lastLine_ = n;
        const auto tokens = util::splitWhitespace(line);
        if (tokens.empty()) return;  // blank lines are allowed anywhere
        const std::string_view key = tokens[0];

        switch (state_) {
            case State::ExpectSolid:
                require(equalsIgnoreCase(key, "solid"), n, "file must start with 'solid'");
                mesh_.solidName = joinRest(tokens, 1);
                state_ = State::ExpectFacetOrEnd;
                break;
            case State::ExpectFacetOrEnd:
                if (equalsIgnoreCase(key, "endsolid")) {
                    state_ = State::Done;
                } else {
                    require(equalsIgnoreCase(key, "facet"), n, "expected 'facet' or 'endsolid', got '" +
                                                                    std::string(key) + "'");
                    require(tokens.size() == 5 && equalsIgnoreCase(tokens[1], "normal"), n,
                            "expected 'facet normal nx ny nz'");
                    current_ = Triangle{};
                    current_.normal = readVec3(tokens, 2, n, "facet normal");
                    vertexCount_ = 0;
                    state_ = State::ExpectOuterLoop;
                }
                break;
            case State::ExpectOuterLoop:
                require(tokens.size() == 2 && equalsIgnoreCase(key, "outer") &&
                            equalsIgnoreCase(tokens[1], "loop"),
                        n, "expected 'outer loop'");
                state_ = State::ExpectVertex;
                break;
            case State::ExpectVertex:
                if (equalsIgnoreCase(key, "endloop")) {
                    throw ParseError(n, "facet loop has " + std::to_string(vertexCount_) +
                                            " vertices; exactly 3 are required");
                }
                require(equalsIgnoreCase(key, "vertex") && tokens.size() == 4, n,
                        "expected 'vertex x y z'");
                current_.vertex[vertexCount_++] = readVec3(tokens, 1, n, "vertex");
                if (vertexCount_ == 3) state_ = State::ExpectEndLoop;
                break;
            case State::ExpectEndLoop:
                if (equalsIgnoreCase(key, "vertex")) {
                    throw ParseError(n, "facet loop has more than 3 vertices; only triangles are allowed");
                }
                require(equalsIgnoreCase(key, "endloop"), n, "expected 'endloop'");
                state_ = State::ExpectEndFacet;
                break;
            case State::ExpectEndFacet:
                require(equalsIgnoreCase(key, "endfacet"), n, "expected 'endfacet'");
                commitTriangle(n);
                state_ = State::ExpectFacetOrEnd;
                break;
            case State::Done:
                if (equalsIgnoreCase(key, "solid")) {
                    throw ParseError(n, "multiple solids in one file are not supported");
                }
                throw ParseError(n, "unexpected content after 'endsolid'");
        }
    }

    Vec3 readVec3(const std::vector<std::string_view>& tokens, std::size_t first, std::size_t n,
                  const char* what) const {
        double c[3];
        for (int i = 0; i < 3; ++i) {
            const auto value = util::parseDouble(tokens[first + i]);
            if (!value) {
                throw ParseError(n, std::string(what) + " component '" + std::string(tokens[first + i]) +
                                        "' is not a number");
            }
            if (!std::isfinite(*value)) {
                throw ParseError(n, std::string(what) + " component '" + std::string(tokens[first + i]) +
                                        "' is not finite (NaN/Inf are rejected)");
            }
            c[i] = *value;
        }
        return {c[0], c[1], c[2]};
    }

    void commitTriangle(std::size_t n) {
        if (mesh_.triangles.size() >= limits_.maxTriangles) {
            throw ParseError(n, "triangle limit of " + std::to_string(limits_.maxTriangles) + " exceeded");
        }
        const Vec3 areaVector =
            cross(sub(current_.vertex[1], current_.vertex[0]), sub(current_.vertex[2], current_.vertex[0]));
        const double twiceArea = length(areaVector);
        if (twiceArea == 0.0) ++mesh_.degenerateTriangles;
        mesh_.surfaceArea += 0.5 * twiceArea;
        if (length(current_.normal) == 0.0 && twiceArea > 0.0) {
            current_.normal = {areaVector.x / twiceArea, areaVector.y / twiceArea, areaVector.z / twiceArea};
            ++mesh_.recomputedNormals;
        }
        mesh_.triangles.push_back(current_);
    }

    void finalise() {
        constexpr double inf = std::numeric_limits<double>::infinity();
        BoundingBox box{{inf, inf, inf}, {-inf, -inf, -inf}};
        for (const auto& t : mesh_.triangles) {
            for (const auto& v : t.vertex) {
                box.min = {std::min(box.min.x, v.x), std::min(box.min.y, v.y), std::min(box.min.z, v.z)};
                box.max = {std::max(box.max.x, v.x), std::max(box.max.y, v.y), std::max(box.max.z, v.z)};
            }
        }
        mesh_.bounds = box;
        if (mesh_.degenerateTriangles > 0) {
            mesh_.warnings.push_back(std::to_string(mesh_.degenerateTriangles) +
                                     " degenerate (zero-area) triangle(s) were kept");
        }
        if (mesh_.recomputedNormals > 0) {
            mesh_.warnings.push_back(std::to_string(mesh_.recomputedNormals) +
                                     " zero normal(s) recomputed from vertex winding");
        }
    }

    static void require(bool condition, std::size_t n, const std::string& message) {
        if (!condition) throw ParseError(n, message);
    }

    static std::string joinRest(const std::vector<std::string_view>& tokens, std::size_t from) {
        std::string out;
        for (std::size_t i = from; i < tokens.size(); ++i) {
            if (!out.empty()) out += ' ';
            out.append(tokens[i]);
        }
        return out;
    }

    std::string describeExpectation() const {
        switch (state_) {
            case State::ExpectSolid: return "no 'solid' keyword found";
            case State::ExpectFacetOrEnd: return "missing 'endsolid'";
            case State::ExpectOuterLoop: return "facet without 'outer loop'";
            case State::ExpectVertex: return "facet loop has fewer than 3 vertices";
            case State::ExpectEndLoop: return "missing 'endloop'";
            case State::ExpectEndFacet: return "missing 'endfacet'";
            case State::Done: return "complete";
        }
        return "unknown";
    }

    std::string_view text_;
    StlLimits limits_;
    StlMesh mesh_;
    State state_ = State::ExpectSolid;
    Triangle current_;
    std::size_t vertexCount_ = 0;
    std::size_t lastLine_ = 0;
};

}  // namespace

Vec3 BoundingBox::centre() const noexcept {
    return {(min.x + max.x) / 2, (min.y + max.y) / 2, (min.z + max.z) / 2};
}

double BoundingBox::diagonal() const noexcept { return length(sub(max, min)); }

StlMesh parseAsciiStl(std::string_view text, const StlLimits& limits) {
    return StlParser(text, limits).run();
}

StlMesh loadAsciiStlFile(const std::string& path, const StlLimits& limits) {
    const std::string content = util::readFileLimited(path, limits.maxBytes);
    return parseAsciiStl(content, limits);
}

}  // namespace wb::geometry
