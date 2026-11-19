#include "algo/Palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace painterbot {
namespace {

using Lab = std::array<double, 3>;

Lab Perceptual(Rgb rgb)
{
    static const auto linear = [] {
        std::array<double, 256> table { };
        for (int i = 0; i < 256; ++i) {
            const double v = i / 255.0;
            table[i] = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
        }
        return table;
    }();
    const double r = linear[rgb.red], g = linear[rgb.green], b = linear[rgb.blue];
    const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    return { 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s };
}

int Key(Rgb c) { return (int(c.red) << 16) | (int(c.green) << 8) | c.blue; }

struct Sample {
    Rgb color;
    Lab lab;
    double weight;
    int label { };
};

struct Moments {
    double weight { };
    Lab sum { }, squares { };
    void Add(const Sample& s)
    {
        weight += s.weight;
        for (int d = 0; d < 3; ++d) {
            sum[d] += s.weight * s.lab[d];
            squares[d] += s.weight * s.lab[d] * s.lab[d];
        }
    }
};

struct Box {
    int begin, end, axis;
    double variance;
    bool operator<(const Box& b) const
    {
        if (variance == b.variance) {
            return begin > b.begin;
        }
        return variance < b.variance;
    }
};

Box Measure(const std::vector<Sample>& samples, int begin, int end)
{
    Moments m;
    for (int i = begin; i < end; ++i) {
        m.Add(samples[i]);
    }
    Box box { begin, end, 0, 0 };
    double largest = -1;
    for (int d = 0; d < 3; ++d) {
        const double variance = std::max(0.0, m.squares[d] - m.sum[d] * m.sum[d] / m.weight);
        box.variance += variance;
        if (variance > largest) {
            box.axis = d;
            largest = variance;
        }
    }
    if (end - begin == 1) {
        box.variance = -1;
    }
    return box;
}

int Split(std::vector<Sample>& samples, const Box& box)
{
    const int d = box.axis;
    std::sort(samples.begin() + box.begin, samples.begin() + box.end, [d](const Sample& a, const Sample& b) {
        if (a.lab[d] == b.lab[d]) {
            return Key(a.color) < Key(b.color);
        }
        return a.lab[d] < b.lab[d];
    });
    Moments total, left;
    for (int i = box.begin; i < box.end; ++i) {
        total.Add(samples[i]);
    }
    double best = -1;
    int cut = box.begin + 1;
    for (int i = box.begin; i + 1 < box.end; ++i) {
        left.Add(samples[i]);
        const double rightWeight = total.weight - left.weight;
        double gain = 0;
        for (int c = 0; c < 3; ++c) {
            const double delta = left.sum[c] / left.weight - (total.sum[c] - left.sum[c]) / rightWeight;
            gain += delta * delta;
        }
        gain *= left.weight * rightWeight;
        if (gain > best) {
            best = gain;
            cut = i + 1;
        }
    }
    return cut;
}

struct ColorMean {
    double r { }, g { }, b { }, weight { };
    void Add(const Sample& s)
    {
        r += s.color.red * s.weight;
        g += s.color.green * s.weight;
        b += s.color.blue * s.weight;
        weight += s.weight;
    }
    Rgb Color() const
    {
        return { static_cast<std::uint8_t>(std::lround(r / weight)),
            static_cast<std::uint8_t>(std::lround(g / weight)),
            static_cast<std::uint8_t>(std::lround(b / weight)) };
    }
};

// A median kd-tree over palette entries bounds the cost of assigning a large histogram.
class PaletteTree {
    struct Node {
        Lab lab;
        int index;
    };
    std::vector<Node> nodes_;

    void Build(int begin, int end, int axis)
    {
        if (begin >= end) {
            return;
        }
        const int mid = (begin + end) / 2;
        std::nth_element(nodes_.begin() + begin, nodes_.begin() + mid, nodes_.begin() + end,
            [axis](const Node& a, const Node& b) {
                if (a.lab[axis] == b.lab[axis]) {
                    return a.index < b.index;
                }
                return a.lab[axis] < b.lab[axis];
            });
        Build(begin, mid, (axis + 1) % 3);
        Build(mid + 1, end, (axis + 1) % 3);
    }

    void Search(const Lab& q, int begin, int end, int axis, double& distance, int& index) const
    {
        if (begin >= end) {
            return;
        }
        const int mid = (begin + end) / 2;
        const Node& n = nodes_[mid];
        double error = 0;
        for (int d = 0; d < 3; ++d) {
            error += (q[d] - n.lab[d]) * (q[d] - n.lab[d]);
        }
        if (error < distance || (error == distance && n.index < index)) {
            distance = error;
            index = n.index;
        }
        const double delta = q[axis] - n.lab[axis];
        const bool lower = delta < 0;
        const int next = (axis + 1) % 3;
        Search(q, lower ? begin : mid + 1, lower ? mid : end, next, distance, index);
        if (delta * delta <= distance) {
            Search(q, lower ? mid + 1 : begin, lower ? end : mid, next, distance, index);
        }
    }

public:
    explicit PaletteTree(const std::vector<Rgb>& colors)
    {
        for (int i = 0; i < static_cast<int>(colors.size()); ++i) {
            nodes_.push_back({ Perceptual(colors[i]), i });
        }
        Build(0, static_cast<int>(nodes_.size()), 0);
    }
    int Nearest(const Lab& q) const
    {
        double distance = std::numeric_limits<double>::infinity();
        int index = 0;
        Search(q, 0, static_cast<int>(nodes_.size()), 0, distance, index);
        return index;
    }
};

PaintMix Mix(Rgb c)
{
    const double hi = std::max({ c.red, c.green, c.blue }), white = std::min({ c.red, c.green, c.blue });
    const double cyan = hi - c.red, magenta = hi - c.green, yellow = hi - c.blue, black = 255 - hi;
    const double sum = cyan + magenta + yellow + black + white;
    return { cyan / sum, magenta / sum, yellow / sum, black / sum, white / sum };
}

} // namespace

void CheckCanceled(std::stop_token stop)
{
    if (stop.stop_requested()) {
        throw std::runtime_error("Preparation canceled.");
    }
}

ColorTone Tone(Rgb color)
{
    const auto lab = Perceptual(color);
    if (std::hypot(lab[1], lab[2]) < 0.025) {
        return { 0, lab[0] };
    }
    constexpr double tau = 6.283185307179586;
    const int sector = static_cast<int>(std::floor(12 * std::atan2(lab[2], lab[1]) / tau + 0.5));
    return { 1 + (sector + 12) % 12, lab[0] };
}

std::vector<std::uint16_t> QuantizeSource(
    const Raster& source, int limit, std::vector<PaletteColor>& palette, std::stop_token stop)
{
    std::unordered_map<int, std::uint32_t> histogram;
    for (std::size_t i = 0; i < source.pixels.size(); ++i) {
        if ((i & 65535) == 0) {
            CheckCanceled(stop);
        }
        ++histogram[Key(source.pixels[i])];
    }
    std::vector<Sample> samples;
    samples.reserve(histogram.size());
    for (auto [key, count] : histogram) {
        const Rgb c { std::uint8_t(key >> 16), std::uint8_t(key >> 8), std::uint8_t(key) };
        samples.push_back({ c, Perceptual(c), std::sqrt(double(count)) });
    }
    std::sort(samples.begin(), samples.end(),
        [](const Sample& a, const Sample& b) { return Key(a.color) < Key(b.color); });
    std::vector<Rgb> colors;
    if (samples.size() <= static_cast<std::size_t>(limit)) {
        for (auto& sample : samples) {
            sample.label = static_cast<int>(colors.size());
            colors.push_back(sample.color);
        }
    } else {
        std::priority_queue<Box> boxes;
        boxes.push(Measure(samples, 0, static_cast<int>(samples.size())));
        while (static_cast<int>(boxes.size()) < limit) {
            CheckCanceled(stop);
            const auto box = boxes.top();
            boxes.pop();
            const int cut = Split(samples, box);
            boxes.push(Measure(samples, box.begin, cut));
            boxes.push(Measure(samples, cut, box.end));
        }
        while (!boxes.empty()) {
            const auto box = boxes.top();
            boxes.pop();
            ColorMean mean;
            for (int i = box.begin; i < box.end; ++i) {
                mean.Add(samples[i]);
            }
            colors.push_back(mean.Color());
        }
        for (int pass = 0; pass < 3; ++pass) {
            PaletteTree tree(colors);
            std::vector<ColorMean> means(colors.size());
            for (std::size_t i = 0; i < samples.size(); ++i) {
                if ((i & 4095) == 0) {
                    CheckCanceled(stop);
                }
                auto& sample = samples[i];
                sample.label = tree.Nearest(sample.lab);
                means[sample.label].Add(sample);
            }
            if (pass < 2) {
                for (std::size_t i = 0; i < colors.size(); ++i) {
                    if (means[i].weight > 0) {
                        colors[i] = means[i].Color();
                    }
                }
            }
        }
    }
    std::vector<int> used(colors.size(), -1);
    for (const auto& s : samples) {
        auto& index = used[s.label];
        if (index < 0) {
            index = static_cast<int>(palette.size());
            palette.push_back({ colors[s.label], Mix(colors[s.label]) });
        }
        histogram[Key(s.color)] = index;
    }
    std::vector<std::uint16_t> labels(source.pixels.size());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if ((i & 65535) == 0) {
            CheckCanceled(stop);
        }
        labels[i] = static_cast<std::uint16_t>(histogram.at(Key(source.pixels[i])));
    }
    return labels;
}
} // namespace painterbot
