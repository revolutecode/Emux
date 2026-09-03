#include "Emux/Builder/IRBuilder.hpp"
#include "Emux/Generator/NasmGenerator.hpp"
#include <Emux/Core/Application.hpp>
#include <Emux/Core/Logger.hpp>

#include <Emux/CLI/Arguments.hpp>
#include <Emux/CLI/FileReader.hpp>

#include <Emux/Compiler/Diagnostics.hpp>
#include <Emux/Compiler/CompilerContext.hpp>
#include <Emux/Compiler/Lexer.hpp>
#include <Emux/Compiler/Parser.hpp>

#include <Emux/Compiler/Semantic/TypeParser.hpp>
#include <Emux/Compiler/Semantic/SemanticAnalyzer.hpp>

#include <cstdlib>
#include <iostream>

namespace Emux
{

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

    if(args.file.empty())
    {
        Logger::Error(
            "No input file specified."
        );

        return EXIT_FAILURE;
    }

    try
    {
        CompilerContext context;

        context.Arguments = args;
        context.Source = FileReader::Read(context.Arguments.file);

        Logger::Info("File loaded successfully.");

        Lexer lexer(context);
        lexer.Tokenize();

        Parser parser(context);
        parser.Parse();

        if(context.Diagnostics.HasErrors()) {
            context.Diagnostics.Print();
            return EXIT_FAILURE;
        }

        SemanticAnalyzer semantic(context);
        semantic.Analyze();

        if(context.Diagnostics.HasErrors())
        {
            context.Diagnostics.Print();
            return EXIT_FAILURE;
        }

        IRBuilder builder;
        std::string code = builder.Build(*context.AST);
    
        NasmGenerator generator;
        generator.Generate(code, "output");
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
    
    Logger::Info(
        "Finished successfully."
    );

    return EXIT_SUCCESS;
}

}
