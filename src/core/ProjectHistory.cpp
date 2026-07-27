#include "core/ProjectHistory.h"

#include <algorithm>
#include <utility>

namespace dawhermes::core {

ProjectHistory::ProjectHistory(std::size_t maxDepth)
    : maxDepth_(std::max<std::size_t>(1, maxDepth))
{
}

void ProjectHistory::clear()
{
    commands_.clear();
    nextCommandIndex_ = 0;
}

bool ProjectHistory::canUndo() const
{
    return nextCommandIndex_ > 0 && nextCommandIndex_ <= commands_.size();
}

bool ProjectHistory::canRedo() const
{
    return nextCommandIndex_ < commands_.size();
}

std::string ProjectHistory::undoLabel() const
{
    if (!canUndo()) {
        return {};
    }

    return commands_[nextCommandIndex_ - 1]->label();
}

std::string ProjectHistory::redoLabel() const
{
    if (!canRedo()) {
        return {};
    }

    return commands_[nextCommandIndex_]->label();
}

bool ProjectHistory::undo()
{
    if (!canUndo()) {
        return false;
    }

    const auto commandIndex = nextCommandIndex_ - 1;
    if (!commands_[commandIndex]->undo()) {
        return false;
    }

    nextCommandIndex_ = commandIndex;
    return true;
}

bool ProjectHistory::redo()
{
    if (!canRedo()) {
        return false;
    }

    if (!commands_[nextCommandIndex_]->redo()) {
        return false;
    }

    ++nextCommandIndex_;
    return true;
}

void ProjectHistory::pushExecuted(std::unique_ptr<ProjectEditCommand> command)
{
    if (command == nullptr) {
        return;
    }

    if (nextCommandIndex_ < commands_.size()) {
        commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(nextCommandIndex_), commands_.end());
    }

    commands_.push_back(std::move(command));
    nextCommandIndex_ = commands_.size();

    while (commands_.size() > maxDepth_) {
        commands_.erase(commands_.begin());
        if (nextCommandIndex_ > 0) {
            --nextCommandIndex_;
        }
    }
}

std::size_t ProjectHistory::size() const
{
    return commands_.size();
}

}  // namespace dawhermes::core
