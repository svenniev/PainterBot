#include "commands/CommandProcessor.h"

#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <utility>

namespace painterbot {
namespace {

template <typename T>
class ValueAction final : public IAction {
public:
    ValueAction(T& target, T value)
        : target_(target), before_(target), after_(std::move(value)) {}

    void Do() noexcept override { target_ = after_; }
    void Undo() noexcept override { target_ = before_; }

private:
    T& target_;
    T before_;
    T after_;
};

std::string_view Trim(std::string_view text)
{
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

std::string_view ReadWord(std::string_view& text)
{
    const auto end = text.find_first_of(" \t");
    const auto word = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{} : Trim(text.substr(end));
    return word;
}

template <typename T>
T ReadNumber(std::string_view& text)
{
    const auto word = ReadWord(text);
    if (word.empty()) {
        throw std::invalid_argument("Expected an integer argument.");
    }
    T number{};
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), number);
    if (error != std::errc{} || end != word.data() + word.size()) {
        throw std::invalid_argument("Expected an integer argument.");
    }
    return number;
}

void RequireEnd(std::string_view text)
{
    if (!text.empty()) {
        throw std::invalid_argument("Unexpected arguments after command.");
    }
}

void CheckCancellation(std::stop_token stop)
{
    if (stop.stop_requested()) {
        throw std::runtime_error("Command canceled.");
    }
}

}  // namespace

std::filesystem::path PathFromUtf8(std::string_view path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

Command ParseCommand(std::string_view line)
{
    line = Trim(line);
    const auto command = ReadWord(line);
    if (command == "load") {
        if (line.starts_with('"') && line.size() > 1 && line.ends_with('"')) {
            line = line.substr(1, line.size() - 2);
        }
        if (line.empty() || line.find('"') != std::string_view::npos) {
            throw std::invalid_argument("Expected an image path after load.");
        }
        return LoadReference{PathFromUtf8(line)};
    }
    if (command == "prepare") {
        if (line.empty()) {
            return Prepare{};
        }
        const PaintSettings settings{ReadNumber<int>(line), ReadNumber<int>(line), ReadNumber<int>(line)};
        RequireEnd(line);
        return Prepare{settings};
    }
    if (command == "seek-patch") {
        const auto count = ReadNumber<std::size_t>(line);
        RequireEnd(line);
        return SeekPatches{count};
    }
    if (command == "seek") {
        const auto count = ReadNumber<std::size_t>(line);
        RequireEnd(line);
        return SeekPreview{count};
    }
    if (command == "tab") {
        if (line == "prepare") {
            return SelectTab{Tab::Prepare};
        }
        if (line == "plot") {
            return SelectTab{Tab::Plot};
        }
        throw std::invalid_argument("Expected tab prepare or tab plot.");
    }
    RequireEnd(line);
    if (command == "undo") {
        return Undo{};
    }
    if (command == "redo") {
        return Redo{};
    }
    throw std::invalid_argument("Unknown command.");
}

CommandProcessor::CommandProcessor(ImageLoader load, std::unique_ptr<const IAlgorythm> algorithm)
    : load_(std::move(load)), algorithm_(std::move(algorithm))
{
    if (!load_ || !algorithm_) {
        throw std::invalid_argument("Command processing requires an image loader and paint algorithm.");
    }
}

void CommandProcessor::Execute(const Command& command, std::stop_token stop)
{
    CheckCancellation(stop);
    std::visit([this, stop](const auto& request) { ExecuteOne(request, stop); }, command);
}

void CommandProcessor::Commit(Document next)
{
    auto action = std::make_unique<ValueAction<Document>>(model_.document_, std::move(next));
    if (!CanRedo()) {
        history_.push_back(std::move(action));
    } else {
        history_[historyPosition_] = std::move(action);
        history_.resize(historyPosition_ + 1);
    }
    history_[historyPosition_]->Do();
    ++historyPosition_;
}

void CommandProcessor::ExecuteOne(const LoadReference& command, std::stop_token stop)
{
    auto reference = std::make_shared<const ReferenceImage>(load_(command.path));
    if (!reference->source.IsValid() || !reference->thumbnail.IsValid()) {
        throw std::runtime_error("The reference image could not be decoded.");
    }
    CheckCancellation(stop);
    Commit({std::move(reference), model_.document_.settings, {}, 0});
}

void CommandProcessor::ExecuteOne(const Prepare& command, std::stop_token stop)
{
    const auto& document = model_.document_;
    if (!document.reference) {
        throw std::runtime_error("Open a reference image before preparing a painting.");
    }
    const auto settings = command.settings.value_or(document.settings);
    Validate(settings);
    auto plan = std::make_shared<const PaintPlan>(algorithm_->Prepare(document.reference->source, settings, stop));
    CheckCancellation(stop);
    const auto strokeCount = plan->strokes.size();
    const auto patchCount = plan->segments.size();
    Commit({document.reference, settings, std::move(plan), strokeCount, patchCount});
}

void CommandProcessor::ExecuteOne(const SeekPreview& command, std::stop_token)
{
    auto& document = model_.document_;
    const auto strokeCount = document.plan ? document.plan->strokes.size() : 0;
    if (command.strokeCount > strokeCount) {
        throw std::invalid_argument("Preview position is outside the painting sequence.");
    }
    Document next = document;
    next.visibleStrokes = command.strokeCount;
    if (document.plan && command.strokeCount > 0) {
        const auto& patches = document.plan->segments;
        const auto last = std::upper_bound(patches.begin(), patches.end(), command.strokeCount,
            [](std::size_t count, const Segment& patch) { return count < patch.firstStroke + patch.strokeCount; });
        next.visiblePatches = last - patches.begin();
    } else {
        next.visiblePatches = 0;
    }
    ValueAction(document, std::move(next)).Do();
}

void CommandProcessor::ExecuteOne(const SeekPatches& command, std::stop_token)
{
    auto& document = model_.document_;
    if (command.count > (document.plan ? document.plan->segments.size() : 0))
        throw std::invalid_argument("Patch position is outside the painting sequence.");
    Document next = document;
    next.visiblePatches = command.count;
    next.visibleStrokes = 0;
    if (command.count) {
        const auto& patch = document.plan->segments[command.count - 1];
        next.visibleStrokes = patch.firstStroke + patch.strokeCount;
    }
    ValueAction(document, std::move(next)).Do();
}

void CommandProcessor::ExecuteOne(const SelectTab& command, std::stop_token)
{
    ValueAction(model_.tab_, command.tab).Do();
}

void CommandProcessor::ExecuteOne(Undo, std::stop_token)
{
    if (CanUndo()) {
        history_[--historyPosition_]->Undo();
    }
}

void CommandProcessor::ExecuteOne(Redo, std::stop_token)
{
    if (CanRedo()) {
        history_[historyPosition_++]->Do();
    }
}

}  // namespace painterbot
