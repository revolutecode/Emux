#include "Emux/Core/ObjectFile.hpp"
#include "Emux/Core/Version.hpp"
#include "Emux/Utilities/Hash.hpp"

#include <cstring>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

namespace Emux
{

namespace
{

constexpr char kMagic[8] = { 'E', 'M', 'U', 'X', 'O', 'B', 'J', '1' };
constexpr std::uint32_t kMaxHeaderSize = 16u * 1024u * 1024u;

struct FileHeader
{
    char Magic[8];
    std::uint32_t Version = 0;
    std::uint32_t HeaderSize = 0;
    std::uint64_t CodeSize = 0;
    std::uint64_t CodeHash = 0;
};

std::uint64_t CodeHash(const std::vector<std::uint8_t>& code)
{
    Hash::Hasher hasher;
    hasher.Add(std::string_view(
        reinterpret_cast<const char*>(code.data()),
        code.size()
    ));
    return hasher.Value();
}

}

bool WriteObject(
    const std::filesystem::path& path,
    const ObjectInfo& info,
    std::string& error
)
{
    nlohmann::json header;
    header["module"] = info.Module;
    header["arch"] = info.Arch;
    header["key"] = info.Key;
    header["ir"] = info.Ir;

    const std::string headerText = header.dump();

    FileHeader fileHeader;
    std::memcpy(fileHeader.Magic, kMagic, sizeof(kMagic));
    fileHeader.Version = kObjectFormatVersion;
    fileHeader.HeaderSize = static_cast<std::uint32_t>(headerText.size());
    fileHeader.CodeSize = static_cast<std::uint64_t>(info.Code.size());
    fileHeader.CodeHash = CodeHash(info.Code);

    std::filesystem::path temporary = path;
    temporary += ".tmp";

    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file)
        {
            error = "Could not create object file - " + temporary.string();
            return false;
        }

        file.write(reinterpret_cast<const char*>(&fileHeader), sizeof(fileHeader));
        file.write(headerText.data(), static_cast<std::streamsize>(headerText.size()));
        if (!info.Code.empty())
        {
            file.write(
                reinterpret_cast<const char*>(info.Code.data()),
                static_cast<std::streamsize>(info.Code.size())
            );
        }

        if (!file)
        {
            error = "Could not write object file - " + temporary.string();
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec)
    {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
        if (ec)
        {
            error = "Could not replace object file - " + path.string();
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }

    return true;
}

std::optional<ObjectInfo> ReadObject(
    const std::filesystem::path& path
)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return std::nullopt;
    }

    FileHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file)
    {
        return std::nullopt;
    }

    if (std::memcmp(header.Magic, kMagic, sizeof(kMagic)) != 0)
    {
        return std::nullopt;
    }

    if (header.Version != kObjectFormatVersion)
    {
        return std::nullopt;
    }

    if (header.HeaderSize > kMaxHeaderSize || header.CodeSize > (1ull << 30))
    {
        return std::nullopt;
    }

    std::string headerText(header.HeaderSize, '\0');
    file.read(headerText.data(), static_cast<std::streamsize>(headerText.size()));
    if (!file)
    {
        return std::nullopt;
    }

    ObjectInfo info;
    try
    {
        const auto json = nlohmann::json::parse(headerText);
        info.Module = json.at("module").get<std::string>();
        info.Arch = json.at("arch").get<std::string>();
        info.Key = json.at("key").get<std::string>();
        info.Ir = json.at("ir").get<std::string>();
    }
    catch (const nlohmann::json::exception&)
    {
        return std::nullopt;
    }

    info.Code.resize(static_cast<std::size_t>(header.CodeSize));
    if (header.CodeSize > 0)
    {
        file.read(
            reinterpret_cast<char*>(info.Code.data()),
            static_cast<std::streamsize>(info.Code.size())
        );
        if (!file)
        {
            return std::nullopt;
        }
    }

    if (CodeHash(info.Code) != header.CodeHash)
    {
        return std::nullopt;
    }

    return info;
}

}
