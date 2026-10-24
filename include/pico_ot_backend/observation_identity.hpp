#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
namespace pico_ot {
std::string NewObservationUuid();
struct SourceSpaceState { uint32_t revision=0; bool known=false; };
// Single-owner PC service holds its instance mutex for this file's lifetime.
class ObservationIdentity {
public:
    explicit ObservationIdentity(std::filesystem::path path);
    [[nodiscard]] const std::string& SourceId() const noexcept { return source_; }
    [[nodiscard]] const std::string& SpaceId() const noexcept { return space_; }
    [[nodiscard]] uint32_t Revision() const noexcept { return revision_; }
    uint32_t AdvanceRevision();
    SourceSpaceState RefreshSpace(uint64_t senderSession);
    [[nodiscard]] const std::string& Generation()const noexcept{return generation_;}
    [[nodiscard]] bool SpaceKnown()const noexcept{return known_;}
private:
    std::filesystem::path path_;
    std::string source_, space_;
    uint32_t revision_ = 0;
    std::string generation_;
    bool known_=false;
    void Save(uint32_t revision);
};
}
