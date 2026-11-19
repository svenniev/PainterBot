#pragma once

#include "algo/Palette.h"
#include <algorithm>

namespace painterbot::detail {

constexpr void IndexFills(PaintPlan& plan, const std::stop_token* stop = nullptr)
{
    auto& art = plan.artwork;
    art.bands.resize(2 * art.height);
    for (std::uint32_t id = 0; id < plan.segments.size(); ++id) {
        if ((id & 1023) == 0 && stop) {
            CheckCanceled(*stop);
        }
        const auto& patch = plan.segments[id];
        for (auto ring = patch.firstRing; ring < patch.firstRing + patch.ringCount; ++ring) {
            const auto end = art.ringOffsets[ring + 1];
            Vertex a = art.vertices[end - 1];
            for (auto i = art.ringOffsets[ring]; i < end; ++i) {
                const Vertex b = art.vertices[i];
                // A downward edge is the right boundary of its fill, including hole edges.
                if (a.y2 < b.y2) {
                    // All traced edges are vertical, horizontal or 45-degree diagonals.
                    const int slope = (b.x2 - a.x2) / (b.y2 - a.y2);
                    for (int y = a.y2; y < b.y2; ++y) {
                        const int x = a.x2 + (y - a.y2) * slope;
                        art.bands[y].push_back({ x, x + slope, id });
                    }
                }
                a = b;
            }
        }
    }
    for (auto& band : art.bands) {
        if (stop) {
            CheckCanceled(*stop);
        }
        std::sort(band.begin(), band.end(), [](const FillEdge& a, const FillEdge& b) {
            return std::int64_t(a.xTop2) + a.xBottom2 < std::int64_t(b.xTop2) + b.xBottom2;
        });
    }
}

// An enclosed single-ring island can be overpainted if its entire area equals the hole
// and it comes later in the schedule. Composite holes keep their original boundaries.
constexpr void FindUndercoats(PaintPlan& plan, const std::stop_token* stop = nullptr)
{
    auto& art = plan.artwork;
    art.paintThrough.assign(art.ringAreas.size(), false);
    for (std::uint32_t id = 0; id < plan.segments.size(); ++id) {
        if ((id & 1023) == 0 && stop) {
            CheckCanceled(*stop);
        }
        const auto& parent = plan.segments[id];
        for (auto r = parent.firstRing; r < parent.firstRing + parent.ringCount; ++r) {
            if (art.ringAreas[r] >= 0) {
                continue;
            }
            const Vertex a = art.vertices[art.ringOffsets[r]], b = art.vertices[art.ringOffsets[r] + 1];
            // Probe just inside the hole, on the left of its directed edge in image coordinates.
            const int nx = (b.y2 > a.y2) - (b.y2 < a.y2);
            const int ny = (a.x2 > b.x2) - (a.x2 < b.x2);
            const auto owner = art.PatchAt(
                0.25 * (double(a.x2) + b.x2) + nx * 0.0625, 0.25 * (double(a.y2) + b.y2) + ny * 0.0625);
            auto& island = plan.segments[owner];
            if (owner > id && island.ringCount == 1 && island.area == -art.ringAreas[r]) {
                art.paintThrough[r] = true;
                island.undercoat = id;
            }
        }
    }
}

} // namespace painterbot::detail
