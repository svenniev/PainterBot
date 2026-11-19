#pragma once

#include "viewmodel/viewmodel.h"

#include <stop_token>

namespace painterbot {

class IAlgorythm {
public:
    virtual ~IAlgorythm() = default;
    [[nodiscard]] virtual PaintPlan Prepare(const Raster& image, const PaintSettings& settings,
                                            std::stop_token stop = {}) const = 0;
};

}  // namespace painterbot
