#pragma once

#include "editor_view.h"
#include <map>
#include <mutex>

namespace TuneLab::Cubase
{
// Private, in-process companion-API association. Tokens are never serialized
// into a project and are not addresses. Registry entries do not own documents.
class EditorBinding final
{
  public:
    EditorBinding()
    {
        std::lock_guard<std::mutex> lock(gate);
        token = ++nextToken;
        documents.emplace(token, std::weak_ptr<EditorDocument>{});
    }
    EditorBinding(const EditorBinding &) = delete;
    EditorBinding &operator=(const EditorBinding &) = delete;
    ~EditorBinding()
    {
        std::lock_guard<std::mutex> lock(gate);
        documents.erase(token);
    }
    std::int64_t id() const noexcept
    {
        return token;
    }
    void select(const std::shared_ptr<EditorDocument> &document)
    {
        std::lock_guard<std::mutex> lock(gate);
        documents.at(token) = document;
    }
    static bool resolve(std::int64_t token, std::shared_ptr<EditorDocument> &document)
    {
        std::lock_guard<std::mutex> lock(gate);
        const auto entry = documents.find(token);
        if (entry == documents.end())
            return false;
        document = entry->second.lock();
        return true;
    }

  private:
    std::int64_t token;
    inline static std::mutex gate;
    inline static std::int64_t nextToken = 0;
    inline static std::map<std::int64_t, std::weak_ptr<EditorDocument>> documents;
};
} // namespace TuneLab::Cubase
