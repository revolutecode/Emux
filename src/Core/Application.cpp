#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Compiler/Preprocessor/Preprocessor.hpp"
#include "Emux/Core/Arch.hpp"
#include "Emux/Core/Config.hpp"
#include "Emux/Generator/NasmGenerator.hpp"
#include <Emux/Core/Application.hpp>
#include <Emux/Core/Logger.hpp>

#include <Emux/CLI/Arguments.hpp>
#include <Emux/CLI/FileReader.hpp>

#include <Emux/Compiler/Diagnostics.hpp>
#include <Emux/Compiler/CompilerContext.hpp>
#include <Emux/Compiler/Lexer/Lexer.hpp>
#include <Emux/Compiler/Parser/Parser.hpp>

#include <Emux/Compiler/Type/TypeParser.hpp>
#include <Emux/Compiler/Semantic/SemanticAnalyzer.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <system_error>

namespace fs = std::filesystem;

namespace Emux
{

bool ProcessErros(CompilerContext& context)
{
    if(context.Diagnostics.HasErrors()) {
        context.Diagnostics.Print();
        Logger::Error("Finished with errors");
        return true;
    }
    return false;
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
        Logger::Info("Emux 0.1.0\n");

        return EXIT_SUCCESS;
    } else if(args.help)
    {
        return EXIT_SUCCESS;
    }

    int exitCode = EXIT_SUCCESS;
    try
    {
        CompilerContext context;
        context.Arguments = args;

        std::optional<Architecture> cliArch;
        if (!args.arch.empty())
        {
            cliArch = ArchFromString(args.arch);
        }

        if (!args.file.empty() && fs::exists(args.file))
        {
            fs::path filePath(args.file);
            fs::path sourceDir = fs::absolute(filePath).parent_path();

            context.Project = Config::Project{
                .name = filePath.filename().string(),
                .type = { "executable" },
                .arch = cliArch.value_or(Architecture::X86_64),
                .output = filePath.stem(),
                .include { sourceDir, "./emux/sdk", "C:/Emux/SDK" },
                .sources { filePath.lexically_normal() }
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

            context.Project = *loaded;
            if (cliArch)
            {
                context.Project.arch = *cliArch;
            }
        }
        else
        {
            Logger::Fatal("Config file not found - " + args.config);
            return EXIT_FAILURE;
        }

        context.Source = FileReader::Read(context.Project.sources[0]);
        context.Sources[context.Project.sources[0].string()] = context.Source;

        if (!args.output.empty())
        {
            context.Project.output = args.output;
        }

        if (context.Project.output.empty())
        {
            Logger::Fatal("Output path is empty - use -o <path> or set build.output in the config.");
            return EXIT_FAILURE;
        }

        auto outputDir = context.Project.output.parent_path();
        if (!outputDir.empty() && !fs::exists(outputDir))
        {
            std::error_code ec;
            fs::create_directories(outputDir, ec);
            if (ec)
            {
                Logger::Fatal("Could not create output directory - " + outputDir.string());
                return EXIT_FAILURE;
            }
        }

        Logger::Info("Project loaded successfully.");

        Lexer lexer(context);
        lexer.Tokenize();

        if(ProcessErros(context)) {
            return EXIT_FAILURE;
        }

        Preprocessor preprocessor(context);
        preprocessor.Process();

        if(ProcessErros(context)) {
            return EXIT_FAILURE;
        }

        Parser parser(context);
        parser.Parse();

        if(ProcessErros(context)) {
            return EXIT_FAILURE;
        }

        SemanticAnalyzer semantic(context);
        semantic.Analyze();

        if(ProcessErros(context))
        {
            return EXIT_FAILURE;
        }

        IRBuilder builder;
        std::string code = builder.Build(*context.AST);
    
        NasmGenerator generator(context.Project.arch);
        if(generator.Generate(code, context.Project.output) != 0)
        {
            return EXIT_FAILURE;
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
