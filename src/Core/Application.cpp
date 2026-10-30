#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Compiler/Preprocessor/Preprocessor.hpp"
#include "Emux/Core/Arch.hpp"
#include "Emux/Core/Config.hpp"
#include "Emux/Core/ObjectFile.hpp"
#include "Emux/Core/Parallel.hpp"
#include "Emux/Core/Subsystem.hpp"
#include "Emux/Core/Version.hpp"
#include "Emux/Generator/NasmToolchain.hpp"
#include "Emux/Generator/X64Generator.hpp"
#include "Emux/Generator/X86Generator.hpp"
#include "Emux/Utilities/Hash.hpp"
#include <Emux/Core/Application.hpp>
#include <Emux/Core/Logger.hpp>

#include <Emux/CLI/Arguments.hpp>
#include <Emux/CLI/FileReader.hpp>

#include <Emux/Compiler/CompilerContext.hpp>
#include <Emux/Compiler/Diagnostics.hpp>
#include <Emux/Compiler/Lexer/Lexer.hpp>
#include <Emux/Compiler/Parser/Parser.hpp>
#include <Emux/Compiler/Semantic/SemanticAnalyzer.hpp>
#include <Emux/Compiler/Semantic/SemanticContext.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace Emux
{

namespace
{

struct BuildModule
{
    fs::path Path;
    std::string Key;
    std::string Name;
    std::string Id;

    CompilerContext Context;

    std::string SourceHash;
    std::string DependenciesHash;
    std::string CacheKey;

    std::string Ir;
    std::vector<std::uint8_t> Object;

    fs::path EmoPath;
    fs::path AsmPath;
    fs::path ObjPath;
    fs::path LogPath;

    bool FromCache = false;
};

std::string ArchName(Architecture arch)
{
    return arch == Architecture::X86 ? "x86" : "x86_64";
}

std::string Sanitize(std::string text)
{
    for (char& c : text)
    {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_')
        {
            c = '_';
        }
    }

    return text.empty() ? "module" : text;
}

fs::path StripOutputExtension(fs::path path)
{
    const std::string ext = path.extension().string();
    if (ext == ".exe" || ext == ".obj" || ext == ".asm" || ext == ".exir")
    {
        path.replace_extension();
    }

    return path;
}

bool PrintErrors(Diagnostics& diagnostics)
{
    if (!diagnostics.HasErrors())
    {
        return false;
    }

    diagnostics.Print();
    Logger::Error("Finished with errors");
    return true;
}

bool WriteBinaryFile(
    const fs::path& path,
    const std::vector<std::uint8_t>& data
)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    if (!data.empty())
    {
        file.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size())
        );
    }

    return static_cast<bool>(file);
}

bool ReadBinaryFile(
    const fs::path& path,
    std::vector<std::uint8_t>& data
)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }

    data.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    );

    return static_cast<bool>(file) || file.eof();
}

std::string ReadTextFile(const fs::path& path)
{
    std::ifstream file(path);
    if (!file)
    {
        return {};
    }

    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

void PrintToolLog(const fs::path& logPath)
{
    const std::string log = ReadTextFile(logPath);
    if (!log.empty())
    {
        Logger::Error("Tool output (" + logPath.string() + "):\n" + log);
    }
}

}

int Application::Run(
    int argc,
    char** argv
)
{
    Logger::Info("Starting Emux...");

    auto args = ParseArguments(argc, argv);

    if(args.version)
    {
        Logger::Info("Emux " + std::string(kEmuxVersion) + "\n");

        return EXIT_SUCCESS;
    } else if(args.help)
    {
        return EXIT_SUCCESS;
    }

    try
    {
        std::optional<Architecture> cliArch;
        if (!args.arch.empty())
        {
            cliArch = ArchFromString(args.arch);
        }

        Config::Project project;

        if (!args.file.empty() && fs::exists(args.file))
        {
            fs::path filePath = fs::absolute(args.file).lexically_normal();
            fs::path sourceDir = filePath.parent_path();

            project = Config::Project{
                .name = filePath.filename().string(),
                .type = { "executable" },
                .arch = cliArch.value_or(Architecture::X86_64),
                .output = filePath.stem(),
                .include { sourceDir, "./emux/sdk", "C:/Emux/SDK" },
                .sources { filePath }
            };
        }
        else if (!args.config.empty() && fs::exists(args.config))
        {
            auto loaded = Config::Load(args.config);
            if (!loaded)
            {
                Logger::Fatal("Could not read config file - " + args.config);
                return EXIT_FAILURE;
            }

            project = *loaded;
            if (cliArch)
            {
                project.arch = *cliArch;
            }
        }
        else
        {
            Logger::Fatal("Config file not found - " + args.config);
            return EXIT_FAILURE;
        }

        if (project.sources.empty())
        {
            Logger::Fatal("No source files - add them to project.sources in the config.");
            return EXIT_FAILURE;
        }

        if (!args.output.empty())
        {
            project.output = args.output;
        }

        if (project.output.empty())
        {
            Logger::Fatal("Output path is empty - use -o <path> or set build.output in the config.");
            return EXIT_FAILURE;
        }

        const fs::path base = StripOutputExtension(project.output);
        const fs::path exePath = base.string() + ".exe";

        Subsystem subsystem = project.subsystem;
        if (!args.subsystem.empty())
        {
            auto parsed = SubsystemFromString(args.subsystem);
            if (!parsed)
            {
                Logger::Fatal("Unknown subsystem '" + args.subsystem + "' (use console or windows).");
                return EXIT_FAILURE;
            }
            subsystem = *parsed;
        }
        Logger::Info(std::string("Subsystem: ") + SubsystemName(subsystem) + ".");

        if (!args.semicolons.empty())
        {
            auto parsed = SemicolonModeFromString(args.semicolons);
            if (!parsed)
            {
                Logger::Fatal("Unknown semicolon mode '" + args.semicolons + "' (use optional or required).");
                return EXIT_FAILURE;
            }
            project.semicolons = *parsed;
        }
        Logger::Info(std::string("Semicolons: ") + SemicolonModeName(project.semicolons) + ".");

        std::error_code ec;
        if (!base.parent_path().empty())
        {
            fs::create_directories(base.parent_path(), ec);
            if (ec)
            {
                Logger::Fatal("Could not create output directory - " + base.parent_path().string());
                return EXIT_FAILURE;
            }
        }

        Logger::Info("Project loaded successfully.");

        // Deduplica os módulos e fixa caminhos absolutos, que são a chave de
        // comparação usada pelo preprocessador para reconhecer @include de módulos.
        std::vector<fs::path> sourcePaths;
        std::unordered_set<std::string> modulePaths;

        for (const fs::path& source : project.sources)
        {
            fs::path absolute = source.is_absolute() ? source : fs::absolute(source);
            absolute = absolute.lexically_normal();

            const std::string key = absolute.string();
            if (!modulePaths.insert(key).second)
            {
                continue;
            }

            if (!fs::exists(absolute))
            {
                Logger::Fatal("Source file not found - " + key);
                return EXIT_FAILURE;
            }

            sourcePaths.push_back(absolute);
        }

        if (sourcePaths.empty())
        {
            Logger::Fatal("No source files - add them to project.sources in the config.");
            return EXIT_FAILURE;
        }

        const unsigned jobs =
            args.jobs != 0 ? args.jobs :
            project.jobs != 0 ? project.jobs :
            Parallel::DefaultJobs();
        Logger::Info("Using " + std::to_string(jobs) + " job(s).");

        std::vector<BuildModule> modules(sourcePaths.size());

        for (std::size_t i = 0; i < sourcePaths.size(); ++i)
        {
            BuildModule& module = modules[i];
            module.Path = sourcePaths[i];
            module.Key = module.Path.string();
            module.Name = module.Path.stem().string();
            module.Id = Sanitize(module.Name) + "-" + Hash::Hex(module.Key).substr(0, 8);

            module.Context.Arguments = args;
            module.Context.Project = project;
            module.Context.ModulePaths = modulePaths;
        }

        // Front-end independente por módulo: lexer + preprocessador + parser.
        Parallel::For(modules.size(), jobs, [&](std::size_t i)
        {
            BuildModule& module = modules[i];

            try
            {
                SourceFile file = FileReader::Read(module.Path);
                module.SourceHash = Hash::Hex(file.GetContent());

                const std::string sourceKey = file.GetName();
                module.Context.Sources[sourceKey] = std::move(file);

                Lexer lexer(module.Context, module.Context.Sources[sourceKey]);
                lexer.Tokenize();

                if (!module.Context.Diagnostics.HasErrors())
                {
                    Preprocessor preprocessor(module.Context);
                    preprocessor.Process();
                }

                if (!module.Context.Diagnostics.HasErrors())
                {
                    Parser parser(module.Context);
                    parser.Parse();
                }
            }
            catch (const std::exception& e)
            {
                module.Context.Diagnostics.Add(
                    DiagnosticLevel::Fatal,
                    {},
                    e.what()
                );
            }
        });

        Diagnostics diagnostics;
        for (BuildModule& module : modules)
        {
            diagnostics.Append(module.Context.Diagnostics);
        }

        if (PrintErrors(diagnostics))
        {
            return EXIT_FAILURE;
        }

        SemanticContext semantic;
        for (BuildModule& module : modules)
        {
            for (const auto& [name, scope] : module.Context.Scopes)
            {
                semantic.Scopes.emplace(name, scope);
            }
        }

        // Declarações de todos os módulos antes de analisar qualquer corpo.
        for (BuildModule& module : modules)
        {
            if (!module.Context.AST) continue;

            SemanticAnalyzer analyzer(*module.Context.AST, semantic, diagnostics, PointerBits(project.arch));
            analyzer.RegisterDeclarations();
        }

        for (BuildModule& module : modules)
        {
            if (!module.Context.AST) continue;

            SemanticAnalyzer analyzer(*module.Context.AST, semantic, diagnostics, PointerBits(project.arch));
            analyzer.AnalyzeBodies();
        }

        ValidateSemantic(semantic, diagnostics);

        if (PrintErrors(diagnostics))
        {
            return EXIT_FAILURE;
        }

        // Bibliotecas pedidas por [[import("...")]] em qualquer módulo.
        std::set<std::string> libraryNames;
        for (const auto& [name, info] : semantic.Imports)
        {
            if (!info.Library.empty())
            {
                libraryNames.insert(info.Library);
            }
        }
        const std::vector<std::string> libraries(libraryNames.begin(), libraryNames.end());

        const std::string archName = ArchName(project.arch);

        Hash::Hasher compilerHasher;
        compilerHasher.AddDelimited(kEmuxVersion);
        compilerHasher.AddDelimited(std::to_string(kObjectFormatVersion));
        compilerHasher.AddDelimited(archName);

        for (const fs::path& include : project.include)
        {
            compilerHasher.AddDelimited(include.string());
        }

        // Recompilar o próprio emux invalida o cache, mesmo sem bumpar a versão.
        const fs::path self = fs::absolute(argv[0]).lexically_normal();
        if (fs::exists(self))
        {
            const fs::file_time_type stamp = fs::last_write_time(self, ec);
            if (!ec)
            {
                compilerHasher.AddDelimited(std::to_string(stamp.time_since_epoch().count()));
            }
        }

        const std::string compilerKey = compilerHasher.Hex();

        std::vector<std::string> interfaceEntries;
        for (const auto& [name, signature] : semantic.Functions)
        {
            std::string entry = "F:" + name + "->" + signature.ReturnType.ToString() + "(";
            for (const Type& type : signature.ParameterTypes)
            {
                entry += type.ToString();
                entry += ',';
            }
            entry += ')';
            interfaceEntries.push_back(std::move(entry));
        }

        for (const auto& [name, type] : semantic.Variables)
        {
            interfaceEntries.push_back("V:" + name + ":" + type.ToString());
        }

        // ABI e símbolo nativo também fazem parte da interface: mudar o import
        // altera o código gerado em todos os módulos que o chamam.
        for (const auto& [name, info] : semantic.Imports)
        {
            interfaceEntries.push_back(
                "I:" + name + ':' + info.Symbol + ':' + info.Library + ':' +
                std::string(AbiName(info.CallingConvention)) + ':' +
                std::to_string(info.StackSize)
            );
        }

        std::sort(interfaceEntries.begin(), interfaceEntries.end());

        Hash::Hasher interfaceHasher;
        for (const std::string& entry : interfaceEntries)
        {
            interfaceHasher.AddDelimited(entry);
        }
        const std::string interfaceHash = interfaceHasher.Hex();

        fs::path cacheRoot = base;
        cacheRoot += ".emux";

        const fs::path objDir = cacheRoot / "obj" / archName;
        const fs::path asmDir = cacheRoot / "asm" / archName;
        const fs::path linkDir = cacheRoot / "link" / archName;

        for (const fs::path& directory : { objDir, asmDir, linkDir })
        {
            fs::create_directories(directory, ec);
            if (ec)
            {
                Logger::Fatal("Could not create cache directory - " + directory.string());
                return EXIT_FAILURE;
            }
        }

        for (BuildModule& module : modules)
        {
            std::vector<std::string> dependencies(
                module.Context.IncludedFiles.begin(),
                module.Context.IncludedFiles.end()
            );
            std::sort(dependencies.begin(), dependencies.end());

            Hash::Hasher dependenciesHasher;
            for (const std::string& dependency : dependencies)
            {
                const auto hash = Hash::FileHex(dependency);
                dependenciesHasher.AddDelimited(dependency);
                dependenciesHasher.AddDelimited(hash.value_or("missing"));
            }
            module.DependenciesHash = dependenciesHasher.Hex();

            Hash::Hasher keyHasher;
            keyHasher.AddDelimited(compilerKey);
            keyHasher.AddDelimited(module.SourceHash);
            keyHasher.AddDelimited(module.DependenciesHash);
            keyHasher.AddDelimited(interfaceHash);
            keyHasher.AddDelimited(module.Key);
            module.CacheKey = keyHasher.Hex();

            module.EmoPath = objDir / (module.Id + ".emo");
            module.AsmPath = asmDir / (module.Id + ".asm");
            module.ObjPath = linkDir / (module.Id + ".obj");
            module.LogPath = module.AsmPath;
            module.LogPath += ".log";
        }

        std::size_t reused = 0;

        for (BuildModule& module : modules)
        {
            if (args.force)
            {
                continue;
            }

            const auto object = ReadObject(module.EmoPath);
            if (object && object->Key == module.CacheKey && object->Arch == archName)
            {
                module.Object = object->Code;
                module.Ir = object->Ir;
                module.FromCache = true;
                ++reused;
                Logger::Info("Reused " + module.Name + " from cache.");
            }
        }

        std::vector<std::string> failures(modules.size());

        Parallel::For(modules.size(), jobs, [&](std::size_t i)
        {
            BuildModule& module = modules[i];
            if (module.FromCache)
            {
                return;
            }

            try
            {
                if (!module.Context.AST)
                {
                    failures[i] = "Module '" + module.Name + "' has no AST.";
                    return;
                }

                IRBuilder builder;
                module.Ir = builder.Build(
                    *module.Context.AST,
                    &semantic.Variables,
                    &semantic.Imports,
                    &semantic.Functions,
                    PointerBits(project.arch)
                );

                std::unique_ptr<IGenerator> generator =
                    project.arch == Architecture::X86
                        ? std::unique_ptr<IGenerator>(std::make_unique<X86Generator>())
                        : std::unique_ptr<IGenerator>(std::make_unique<X64Generator>());

                if (generator->Generate(module.Ir, module.AsmPath) != 0)
                {
                    failures[i] = "Could not generate assembly for module '" + module.Name + "'.";
                    return;
                }

                const int assembleResult = NasmToolchain::Assemble(
                    module.AsmPath,
                    module.ObjPath,
                    project.arch,
                    module.LogPath
                );

                if (assembleResult != 0)
                {
                    failures[i] = "NASM failed for module '" + module.Name + "' (see " + module.LogPath.string() + ").";
                    return;
                }

                if (!ReadBinaryFile(module.ObjPath, module.Object))
                {
                    failures[i] = "Could not read object produced for module '" + module.Name + "'.";
                    return;
                }

                ObjectInfo info;
                info.Module = module.Key;
                info.Arch = archName;
                info.Key = module.CacheKey;
                info.Ir = module.Ir;
                info.Code = module.Object;

                std::string error;
                if (!WriteObject(module.EmoPath, info, error))
                {
                    failures[i] = error;
                }
            }
            catch (const std::exception& e)
            {
                failures[i] = "Module '" + module.Name + "': " + e.what();
            }
        });

        bool failed = false;
        for (std::size_t i = 0; i < modules.size(); ++i)
        {
            if (failures[i].empty())
            {
                continue;
            }

            failed = true;
            Logger::Error(failures[i]);
            PrintToolLog(modules[i].LogPath);
        }

        if (failed)
        {
            return EXIT_FAILURE;
        }

        const std::size_t compiled = modules.size() - reused;

        if (compiled > 0)
        {
            Logger::Info("Compiled " + std::to_string(compiled) + " module(s).");
        }
        if (reused > 0)
        {
            Logger::Info("Reused " + std::to_string(reused) + " module(s) from cache.");
        }

        std::string entry;
        if (semantic.Functions.contains("main::Start"))
        {
            entry = "main_Start";
        }
        else if (semantic.Functions.contains("main::_Start"))
        {
            entry = "main__Start";
        }

        if (entry.empty())
        {
            Logger::Error("Entry point function 'Start' or '_Start' was not emitted.");
            return EXIT_FAILURE;
        }

        {
            fs::path exirPath = base;
            exirPath += ".exir";

            std::ofstream exir(exirPath, std::ios::trunc);
            if (!exir)
            {
                Logger::Error("Could not create file - " + exirPath.string());
                return EXIT_FAILURE;
            }

            for (const BuildModule& module : modules)
            {
                exir << "; module " << module.Key << '\n';
                exir << module.Ir;
                if (module.Ir.empty() || module.Ir.back() != '\n')
                {
                    exir << '\n';
                }
            }
        }

        Hash::Hasher linkHasher;
        linkHasher.AddDelimited(compilerKey);
        linkHasher.AddDelimited(entry);
        linkHasher.AddDelimited(SubsystemName(subsystem));
        linkHasher.AddDelimited(exePath.string());
        for (const std::string& library : libraries)
        {
            linkHasher.AddDelimited(library);
        }
        for (const BuildModule& module : modules)
        {
            linkHasher.AddDelimited(module.CacheKey);
        }
        const std::string linkKey = linkHasher.Hex();

        const fs::path stampPath = cacheRoot / "link.stamp";
        const std::string previousLinkKey = ReadTextFile(stampPath);

        const bool outputExists = fs::exists(exePath);
        const bool needLink =
            args.force ||
            compiled > 0 ||
            !outputExists ||
            previousLinkKey != linkKey;

        if (!needLink)
        {
            Logger::Info("Output is up to date.");
            Logger::Info("Finished successfully.");
            return EXIT_SUCCESS;
        }

        const fs::path entryAsmPath = linkDir / "emux_entry.asm";
        const fs::path entryObjPath = linkDir / "emux_entry.obj";
        const fs::path linkLogPath = linkDir / "link.log";

        if (!NasmToolchain::WriteEntryStub(entryAsmPath, project.arch, entry))
        {
            Logger::Error("Could not create entry stub - " + entryAsmPath.string());
            return EXIT_FAILURE;
        }

        const int entryResult = NasmToolchain::Assemble(
            entryAsmPath,
            entryObjPath,
            project.arch,
            linkLogPath
        );

        if (entryResult != 0)
        {
            Logger::Error("NASM failed for the entry stub.");
            PrintToolLog(linkLogPath);
            return EXIT_FAILURE;
        }

        std::vector<fs::path> objects;
        objects.reserve(modules.size() + 1);
        objects.push_back(entryObjPath);

        for (BuildModule& module : modules)
        {
            if (module.FromCache && !WriteBinaryFile(module.ObjPath, module.Object))
            {
                Logger::Error("Could not materialize object for module '" + module.Name + "'.");
                return EXIT_FAILURE;
            }

            objects.push_back(module.ObjPath);
        }

        Logger::Info("Linking " + exePath.string() + ".");

        fs::remove(stampPath, ec);
        ec.clear();

        const int linkResult = NasmToolchain::Link(
            objects,
            exePath,
            project.arch,
            subsystem,
            libraries,
            linkLogPath
        );

        if (linkResult != 0)
        {
            Logger::Error("Linker (gcc) failed with exit code " + std::to_string(linkResult) + ".");
            PrintToolLog(linkLogPath);
            return EXIT_FAILURE;
        }

        {
            std::ofstream stamp(stampPath, std::ios::trunc);
            if (stamp)
            {
                stamp << linkKey;
            }
        }
    }
    catch(const std::exception& e)
    {
        Logger::Fatal(e.what());
        return EXIT_FAILURE;
    }
    catch(...)
    {
        return EXIT_FAILURE;
    }

    Logger::Info("Finished successfully.");

    return EXIT_SUCCESS;
}

}
