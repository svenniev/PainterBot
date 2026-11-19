#pragma once

#include "algo/IAlgorythm.h"
#include "utils/IAction/IAction.h"

#include <functional>
#include <optional>
#include <string_view>
#include <variant>

namespace painterbot {

struct LoadReference {
    std::filesystem::path path;
};

struct Prepare {
    std::optional<PaintSettings> settings;
};

struct SeekPreview {
    std::size_t strokeCount;
};

struct SeekPatches {
    std::size_t count;
};

struct SelectTab {
    Tab tab;
};

struct Undo {};
struct Redo {};

using Command = std::variant<LoadReference, Prepare, SeekPreview, SeekPatches, SelectTab, Undo, Redo>;
using ImageLoader = std::function<ReferenceImage(const std::filesystem::path&)>;

[[nodiscard]] Command ParseCommand(std::string_view line);
[[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view path);

class CommandProcessor {
public:
    CommandProcessor(ImageLoader load, std::unique_ptr<const IAlgorythm> algorithm);
    CommandProcessor(const CommandProcessor&) = delete;
    CommandProcessor& operator=(const CommandProcessor&) = delete;

    // A caller on a worker thread must hold exclusive access until Execute returns.
    void Execute(const Command& command, std::stop_token stop = {});
    [[nodiscard]] const ViewModel& Model() const noexcept { return model_; }
    [[nodiscard]] bool CanUndo() const noexcept { return historyPosition_ > 0; }
    [[nodiscard]] bool CanRedo() const noexcept { return historyPosition_ < history_.size(); }

private:
    void ExecuteOne(const LoadReference& command, std::stop_token stop);
    void ExecuteOne(const Prepare& command, std::stop_token stop);
    void ExecuteOne(const SeekPreview& command, std::stop_token stop);
    void ExecuteOne(const SeekPatches& command, std::stop_token stop);
    void ExecuteOne(const SelectTab& command, std::stop_token stop);
    void ExecuteOne(Undo, std::stop_token stop);
    void ExecuteOne(Redo, std::stop_token stop);
    void Commit(Document next);

    ImageLoader load_;
    std::unique_ptr<const IAlgorythm> algorithm_;
    ViewModel model_;
    std::vector<std::unique_ptr<IAction>> history_;
    std::size_t historyPosition_{};
};

}  // namespace painterbot
