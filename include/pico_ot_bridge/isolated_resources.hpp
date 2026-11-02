#pragma once
// Internal PC software-test profile, never a wire/SDK contract or implicit mode.
#include <windows.h>
#include <iphlpapi.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include "pico_ot_bridge/service_lifecycle.hpp"

namespace pico_ot::bridge {
struct IsolationProfile {
    std::filesystem::path file, root;
    std::string id;
    DWORD ownerPid = 0;
    std::uint64_t ownerCreated = 0;
    uint16_t ingress = 0, steam = 0, monaka = 0, utility = 0, observation = 0;
    std::wstring Name(const wchar_t* suffix) const {
        return L"Local\\PicoOTBridge_Isolated_" + std::wstring(id.begin(), id.end()) + L"_" + suffix;
    }
    std::string OwnerRecord() const {
        return id + "\n" + std::to_string(ownerPid) + ":" + std::to_string(ownerCreated) + "\n" +
            std::to_string(ingress) + "," + std::to_string(steam) + "," + std::to_string(monaka) + "," +
            std::to_string(utility) + "," + std::to_string(observation) + "\n";
    }
    static IsolationProfile Load(const std::filesystem::path& path) {
        if (!path.is_absolute() || std::filesystem::file_size(path) > 8192) throw std::runtime_error("absolute bounded isolation profile required");
        std::ifstream input(path); std::stringstream bytes; bytes << input.rdbuf();
        if (!input) throw std::runtime_error("isolation profile unreadable");
        std::set<std::string> parsedKeys;
        const auto value = nlohmann::json::parse(bytes.str(), [&](int, nlohmann::json::parse_event_t event, nlohmann::json& item) {
            if (event == nlohmann::json::parse_event_t::key && !parsedKeys.insert(item.get<std::string>()).second)
                throw std::runtime_error("duplicate isolation key");
            return true;
        });
        const std::set<std::string> keys{"version","instance_id","state_root","owner_pid","owner_created","ingress_port","steamvr_port","monaka_port","utility_port","observation_port"};
        if (!value.is_object() || parsedKeys != keys || !value["version"].is_number_integer() || value["version"] != 1)
            throw std::runtime_error("complete version1 isolation profile required");
        IsolationProfile p; p.file = path;
        p.id = value.at("instance_id").get<std::string>();
        if (!std::regex_match(p.id, std::regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))) throw std::runtime_error("canonical instance UUID required");
        p.root = std::filesystem::u8path(value.at("state_root").get<std::string>());
        if (!p.root.is_absolute() || p.root.filename() != p.id || p.root.lexically_normal() != p.root ||
            !std::filesystem::is_directory(p.root) || std::filesystem::is_symlink(p.root)) throw std::runtime_error("dedicated canonical isolation root required");
        for(auto node=p.root;!node.empty();) {
            const auto attributes=GetFileAttributesW(node.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("isolation root/ancestor reparse or unavailable");
            const auto parent=node.parent_path();if(parent==node)break;node=parent;
        }
        const auto pid = value.at("owner_pid").get<std::uint64_t>();
        const auto created = value.at("owner_created").get<std::string>();
        if (!value["owner_pid"].is_number_unsigned() || !pid || pid > 0x7fffffffULL || !std::regex_match(created, std::regex("[1-9][0-9]{0,18}")))
            throw std::runtime_error("exact harness owner identity required");
        p.ownerPid = static_cast<DWORD>(pid); p.ownerCreated = std::stoull(created);
        std::set<uint16_t> ports;
        const auto port = [&](const char* key) {
            const auto raw = value.at(key).get<std::uint64_t>();
            if (!value.at(key).is_number_unsigned() || raw < 1024 || raw > 65535 || (raw >= 29765 && raw <= 29768) || raw == 29810 || !ports.insert(static_cast<uint16_t>(raw)).second)
                throw std::runtime_error("distinct nonproduction isolation ports required");
            return static_cast<uint16_t>(raw);
        };
        p.ingress=port("ingress_port"); p.steam=port("steamvr_port"); p.monaka=port("monaka_port"); p.utility=port("utility_port"); p.observation=port("observation_port");
        return p;
    }
    void ValidateOwner() const {
        pc_service_lifecycle::Handle owner(pc_service_lifecycle::OpenIdentity(ownerPid, ownerCreated));
        if (WaitForSingleObject(owner.value, 0) != WAIT_TIMEOUT) throw std::runtime_error("harness owner not alive");
        const auto lease = root / L"owner.claim";
        {
        pc_service_lifecycle::Handle read(CreateFileW(lease.c_str(), GENERIC_READ, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (read.value == INVALID_HANDLE_VALUE) { read.value=nullptr; throw std::runtime_error("harness lease missing"); }
        char record[512]{}; DWORD count=0;
        if (!ReadFile(read.value, record, sizeof(record), &count, nullptr) || std::string(record, count) != OwnerRecord()) throw std::runtime_error("harness lease/profile mismatch");
        } // Close our reader before checking the independently retained lease.
        const auto probe=CreateFileW(lease.c_str(), GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const auto failure=GetLastError();
        if (probe != INVALID_HANDLE_VALUE) { CloseHandle(probe); throw std::runtime_error("harness lease is not retained"); }
        if (failure != ERROR_SHARING_VIOLATION) throw std::runtime_error("harness lease ownership unavailable");
        ULONG size=0; GetExtendedUdpTable(nullptr,&size,FALSE,AF_INET,UDP_TABLE_OWNER_PID,0);
        std::vector<unsigned char> table(size);
        if (GetExtendedUdpTable(table.data(),&size,FALSE,AF_INET,UDP_TABLE_OWNER_PID,0) != NO_ERROR) throw std::runtime_error("endpoint owner census unavailable");
        const auto* rows=reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(table.data());
        for (const auto port : {steam,monaka,utility,observation}) {
            bool found=false;
            for (DWORD i=0;i<rows->dwNumEntries;++i) {
                const auto& row=rows->table[i];
                if (row.dwLocalAddr==htonl(INADDR_LOOPBACK) && ntohs(static_cast<u_short>(row.dwLocalPort))==port && row.dwOwningPid==ownerPid) found=true;
            }
            if (!found) throw std::runtime_error("all isolation output listeners must belong to exact harness owner");
        }
    }
    void ValidateTray(DWORD pid, std::uint64_t created) const {
        ValidateOwner();
        pc_service_lifecycle::Handle owner(pc_service_lifecycle::OpenIdentity(pid,created));
        std::ifstream input(root / L"tray.claim"); std::string record; std::getline(input,record);
        if (!input || record != std::to_string(pid)+":"+std::to_string(created)) throw std::runtime_error("isolated tray owner mismatch");
    }
};
} // namespace pico_ot::bridge
