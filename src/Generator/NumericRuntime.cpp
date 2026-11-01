#include "Emux/Generator/NumericRuntime.hpp"
#include "Emux/Generator/RuntimeSource.hpp"
#include "Emux/Utilities/Hash.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace Emux
{

namespace
{

// Mudar as flags altera o objeto gerado, logo entra na chave da cache.
constexpr const char* kCompileFlags =
    "-c -O2 -std=c11 -ffreestanding -fno-builtin -fno-stack-protector "
    "-fno-asynchronous-unwind-tables -fno-ident -m64";

bool WriteText(const fs::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::trunc | std::ios::binary);
    if (!file)
    {
        return false;
    }

    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(file);
}

} // namespace

std::string NumericRuntimeKey()
{
    Hash::Hasher hasher;
    hasher.AddDelimited("emux-rt-v1");
    hasher.AddDelimited(kCompileFlags);
    hasher.AddDelimited(NumericRuntimeSourceText);
    return hasher.Hex();
}

bool PrepareNumericRuntime(
    const fs::path& directory,
    fs::path& objectPath,
    const fs::path& logPath
)
{
    std::error_code ec;
    fs::create_directories(directory, ec);
    if (ec)
    {
        return false;
    }

    const std::string key = NumericRuntimeKey();
    const fs::path sourcePath = directory / ("emux_rt_" + key + ".c");
    objectPath = directory / ("emux_rt_" + key + ".obj");

    if (fs::exists(objectPath, ec) && !ec)
    {
        return true;
    }

    if (!fs::exists(sourcePath, ec) || ec)
    {
        if (!WriteText(sourcePath, NumericRuntimeSourceText))
        {
            return false;
        }
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temporary = directory / ("emux_rt_" + key + "." + std::to_string(stamp) + ".tmp");

    const std::string command =
        "gcc " + std::string(kCompileFlags) +
        " \"" + sourcePath.string() + "\" -o \"" + temporary.string() + "\"" +
        " > \"" + logPath.string() + "\" 2>&1";

    const int result = std::system(command.c_str());
    if (result != 0)
    {
        fs::remove(temporary, ec);
        return false;
    }

    ec.clear();
    fs::rename(temporary, objectPath, ec);
    if (ec)
    {
        // Outro emux pode ter publicado o objeto entretanto; nesse caso o
        // ficheiro final ja existe e serve.
        fs::remove(temporary, ec);
        ec.clear();
        if (!fs::exists(objectPath, ec) || ec)
        {
            return false;
        }
    }

    return true;
}

} // namespace Emux
