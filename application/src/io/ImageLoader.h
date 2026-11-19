#pragma once

#include "viewmodel/viewmodel.h"

namespace painterbot {

// Register wx image handlers once on the main thread before loading files.
[[nodiscard]] ReferenceImage DecodeReferenceImage(const std::filesystem::path& path);

}  // namespace painterbot
