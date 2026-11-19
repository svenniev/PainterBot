#pragma once

#include "algo/Palette.h"
#include <limits>
#include <stdexcept>

namespace painterbot::detail {
// Clockwise in image coordinates. Each oriented boundary keeps its fill on the right.
constexpr int dx[] { 1, 0, -1, 0 }, dy[] { 0, 1, 0, -1 };
constexpr int ownerX[] { 0, -1, -1, 0 }, ownerY[] { 0, 0, -1, -1 };

class ContourTracer {
public:
    constexpr ContourTracer(const Raster& image, const std::vector<std::uint16_t>& labels, PaintPlan& plan,
        const std::stop_token* stop = nullptr)
        : width_(image.width)
        , height_(image.height)
        , labels_(labels)
        , plan_(plan)
        , stop_(stop)
        , seen_(labels.size())
        , tracedTop_(labels.size())
    {
    }

    constexpr void Trace()
    {
        std::vector<int> cells;
        for (int seed = 0; seed < static_cast<int>(labels_.size()); ++seed) {
            if ((seed & 65535) == 0 && stop_) {
                CheckCanceled(*stop_);
            }
            if (seen_[seed]) {
                continue;
            }
            const int color = labels_[seed];
            cells.clear();
            cells.push_back(seed);
            seen_[seed] = true;
            Segment patch;
            patch.paletteIndex = color;
            patch.firstRing = static_cast<std::uint32_t>(plan_.artwork.ringOffsets.size() - 1);
            for (std::size_t i = 0; i < cells.size(); ++i) {
                if ((i & 65535) == 0 && stop_) {
                    CheckCanceled(*stop_);
                }
                const int cell = cells[i], x = cell % width_, y = cell / width_;
                if (!tracedTop_[cell] && Label(x, y - 1) != color) {
                    TraceRing(x, y, color, patch);
                }
                for (int d = 0; d < 4; ++d) {
                    const int nx = x + dx[d], ny = y + dy[d];
                    if (Label(nx, ny) != color) {
                        continue;
                    }
                    const int n = ny * width_ + nx;
                    if (!seen_[n]) {
                        seen_[n] = true;
                        cells.push_back(n);
                    }
                }
            }
            plan_.segments.push_back(patch);
        }
    }

private:
    constexpr int Label(int x, int y) const
    {
        return x >= 0 && y >= 0 && x < width_ && y < height_ ? labels_[y * width_ + x] : -1;
    }
    constexpr bool Boundary(int x, int y, int d, int color) const
    {
        const int rx = x + ownerX[d], ry = y + ownerY[d];
        return Label(rx, ry) == color && Label(rx + dy[d], ry - dx[d]) != color;
    }

    // Round ordinary two-material turns on both sides of the same interface.
    // Pin image corners, T-junctions and diagonal saddle points.
    constexpr bool Bevel(int x, int y) const
    {
        if (x == 0 || y == 0 || x == width_ || y == height_) {
            return false;
        }
        const int a = Label(x - 1, y - 1), b = Label(x, y - 1), c = Label(x, y), d = Label(x - 1, y);
        return (a == b && (a == c || a == d)) || (c == d && (c == a || c == b));
    }

    constexpr void TraceRing(int sx, int sy, int color, Segment& patch)
    {
        auto& art = plan_.artwork;
        const auto first = art.vertices.size();
        const auto append = [&](Vertex v) {
            if (art.vertices.size() == first || art.vertices.back() != v) {
                art.vertices.push_back(v);
            }
        };
        int x = sx, y = sy, d = 0;
        std::size_t steps = 0;
        do {
            if ((++steps & 65535) == 0 && stop_) {
                CheckCanceled(*stop_);
            }
            if (d == 0) {
                tracedTop_[y * width_ + x] = true;
            }
            x += dx[d];
            y += dy[d];
            int next = (d + 1) % 4;
            if (!Boundary(x, y, next, color)) {
                next = Boundary(x, y, d, color) ? d : (d + 3) % 4;
            }
            if (next != d) {
                if (Bevel(x, y)) {
                    append({ 2 * x - dx[d], 2 * y - dy[d] });
                    append({ 2 * x + dx[next], 2 * y + dy[next] });
                } else {
                    append({ 2 * x, 2 * y });
                }
            }
            d = next;
        } while (x != sx || y != sy || d != 0);
        if (art.vertices.back() == art.vertices[first]) {
            art.vertices.pop_back();
        }

        // Collapse straight diagonal runs as well as axis-aligned runs.
        std::size_t end = first;
        for (std::size_t i = first; i < art.vertices.size(); ++i) {
            const Vertex v = art.vertices[i];
            if (end >= first + 2) {
                const Vertex a = art.vertices[end - 2], b = art.vertices[end - 1];
                if (std::int64_t(b.x2 - a.x2) * (v.y2 - b.y2) == std::int64_t(b.y2 - a.y2) * (v.x2 - b.x2)) {
                    --end;
                }
            }
            art.vertices[end++] = v;
        }
        art.vertices.resize(end);
        if (end > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("Vector path exceeds the vertex index range.");
        }
        Vertex prev = art.vertices.back();
        double area = 0;
        for (std::size_t i = first; i < end; ++i) {
            const Vertex v = art.vertices[i];
            area += double(prev.x2) * v.y2 - double(prev.y2) * v.x2;
            prev = v;
        }
        patch.area += area / 8.0;
        art.ringAreas.push_back(area / 8.0);
        ++patch.ringCount;
        art.ringOffsets.push_back(static_cast<std::uint32_t>(end));
    }

    int width_, height_;
    const std::vector<std::uint16_t>& labels_;
    PaintPlan& plan_;
    const std::stop_token* stop_;
    std::vector<bool> seen_, tracedTop_;
};

} // namespace painterbot::detail
