#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace dawhermes::core {

class ProjectEditCommand {
public:
    virtual ~ProjectEditCommand() = default;

    virtual std::string label() const = 0;
    virtual bool undo() = 0;
    virtual bool redo() = 0;
};

class ProjectHistory {
public:
    explicit ProjectHistory(std::size_t maxDepth = 100);

    void clear();

    bool canUndo() const;
    bool canRedo() const;

    std::string undoLabel() const;
    std::string redoLabel() const;

    bool undo();
    bool redo();

    void pushExecuted(std::unique_ptr<ProjectEditCommand> command);

    std::size_t size() const;

private:
    std::size_t maxDepth_ { 100 };
    std::vector<std::unique_ptr<ProjectEditCommand>> commands_;
    std::size_t nextCommandIndex_ { 0 };
};

}  // namespace dawhermes::core
