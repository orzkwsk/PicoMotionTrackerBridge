#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
namespace pico_ot {
std::string NewObservationUuid();
// Single-owner PC service holds its instance mutex for this file's lifetime.
class ObservationIdentity {
public:
    explicit ObservationIdentity(std::filesystem::path path);
    [[nodiscard]] const std::string& SourceId() const noexcept { return source_; }
    [[nodiscard]] const std::string& SpaceId() const noexcept { return space_; }
    [[nodiscard]] uint32_t Revision() const noexcept { return revision_; }
    uint32_t AdvanceRevision();
private:
    std::filesystem::path path_;
    std::string source_, space_;
    uint32_t revision_ = 0;
    void Save(uint32_t revision);
};
}
