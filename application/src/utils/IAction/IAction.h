#pragma once

namespace painterbot {

class IAction {
public:
    virtual ~IAction() = default;
    virtual void Do() noexcept = 0;
    virtual void Undo() noexcept = 0;
};

}  // namespace painterbot
