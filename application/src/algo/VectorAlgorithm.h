#pragma once

#include "algo/IAlgorythm.h"

namespace painterbot {

class VectorAlgorithm final : public IAlgorythm {
public:
    [[nodiscard]] PaintPlan Prepare(const Raster& image, const PaintSettings& settings,
                                    std::stop_token stop = {}) const override;
};

}  // namespace painterbot
