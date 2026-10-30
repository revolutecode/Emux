#include "Emux/Exir/Exir.hpp"
#include "Emux/Compiler/Type/TypeParser.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Emux::Exir
{

namespace
{

std::string Trim(std::string_view input)
{
    std::size_t begin = 0;
    std::size_t end = input.size();

    while (begin < end && std::isspace(static_cast<unsigned char>(input[begin])))
    {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])))
    {
        --end;
    }

    return std::string(input.substr(begin, end - begin));
}

std::vector<std::string> SplitWhitespace(std::string_view input)
{
    std::vector<std::string> tokens;
    std::istringstream stream{ std::string(input) };
    std::string token;

    while (stream >> token)
    {
        tokens.push_back(std::move(token));
    }

    return tokens;
}

// Separa por vírgulas de nível superior, respeitando aspas e parênteses.
std::vector<std::string> SplitTopLevel(std::string_view input, char separator)
{
    std::vector<std::string> parts;
    std::string current;
    int parentheses = 0;
    bool quoted = false;

    for (char c : input)
    {
        if (c == '"')
        {
            quoted = !quoted;
        }

        if (!quoted)
        {
            if (c == '(') ++parentheses;
            else if (c == ')') --parentheses;
            else if (c == separator && parentheses == 0)
            {
                parts.push_back(Trim(current));
                current.clear();
                continue;
            }
        }

        current += c;
    }

    parts.push_back(Trim(current));
    return parts;
}

std::string FirstWord(std::string& rest)
{
    const std::string text = Trim(rest);
    const std::size_t space = text.find_first_of(" \t");

    if (space == std::string::npos)
    {
        rest = {};
        return text;
    }

    rest = text.substr(space + 1);
    return text.substr(0, space);
}

[[noreturn]] void Fail(std::size_t line, const std::string& message)
{
    throw std::runtime_error("Exir line " + std::to_string(line) + ": " + message);
}

Type ParseTypeOrFail(std::string_view text, std::uint32_t pointerBits, std::size_t line)
{
    auto type = TypeParser::Parse(text, pointerBits);
    if (!type.has_value())
    {
        Fail(line, "invalid type '" + std::string(text) + "'");
    }

    return *type;
}

// Divide "nome : tipo" (espaços opcionais em volta dos dois pontos).
std::pair<std::string, std::string> SplitNameType(std::string_view text, std::size_t line)
{
    const std::size_t colon = text.find(':');
    if (colon == std::string_view::npos)
    {
        Fail(line, "expected '<name> : <type>'");
    }

    const std::string name = Trim(text.substr(0, colon));
    const std::string type = Trim(text.substr(colon + 1));

    if (name.empty() || type.empty())
    {
        Fail(line, "expected '<name> : <type>'");
    }

    return { name, type };
}

BinaryOp ParseBinaryOp(const std::string& op, std::size_t line)
{
    static const std::unordered_map<std::string, BinaryOp> table = {
        { "add", BinaryOp::Add },
        { "sub", BinaryOp::Sub },
        { "mul", BinaryOp::Mul },
        { "div", BinaryOp::Div },
        { "rem", BinaryOp::Rem },
        { "and", BinaryOp::And },
        { "or",  BinaryOp::Or },
        { "xor", BinaryOp::Xor },
        { "shl", BinaryOp::Shl },
        { "shr", BinaryOp::Shr },
        { "sar", BinaryOp::Sar }
    };

    auto it = table.find(op);
    if (it == table.end())
    {
        Fail(line, "unknown binary operation '" + op + "'");
    }

    return it->second;
}

UnaryOp ParseUnaryOp(const std::string& op, std::size_t line)
{
    if (op == "neg") return UnaryOp::Neg;
    if (op == "not") return UnaryOp::Not;
    if (op == "lnot" || op == "lnot32") return UnaryOp::LogicalNot;

    Fail(line, "unknown unary operation '" + op + "'");
}

void ParseCallHeader(
    std::string_view rest,
    Instruction& instruction,
    std::size_t line,
    std::uint32_t pointerBits
)
{
    std::string text = Trim(rest);
    const std::size_t open = text.find('(');

    std::string head = open == std::string::npos ? text : text.substr(0, open);
    std::vector<std::string> tokens = SplitWhitespace(head);

    if (tokens.size() < 3)
    {
        Fail(line, "expected 'call <rettype> <target> <abi> (...)'");
    }

    instruction.ReturnTy = ParseTypeOrFail(tokens[0], pointerBits, line);
    instruction.Target = tokens[1];
    instruction.Convention = AbiFromString(tokens[2]).value_or(Abi::Cdecl);
    instruction.Ty = instruction.ReturnTy;

    if (open == std::string::npos)
    {
        return;
    }

    const std::size_t close = text.rfind(')');
    if (close == std::string::npos || close < open)
    {
        Fail(line, "missing ')' in call");
    }

    const std::string inner = Trim(text.substr(open + 1, close - open - 1));
    if (inner.empty())
    {
        return;
    }

    for (const std::string& part : SplitTopLevel(inner, ','))
    {
        std::vector<std::string> argTokens = SplitWhitespace(part);
        if (argTokens.size() != 2)
        {
            Fail(line, "call argument must be '<type> <value>'");
        }

        instruction.Args.push_back({
            ParseTypeOrFail(argTokens[0], pointerBits, line),
            argTokens[1]
        });
    }
}

Instruction ParseValueInstruction(
    std::string_view rest,
    std::size_t line,
    std::uint32_t pointerBits
)
{
    Instruction instruction;
    std::string text = Trim(rest);
    std::string op = FirstWord(text);

    if (op == "const")
    {
        std::vector<std::string> tokens = SplitWhitespace(text);
        if (tokens.size() < 2)
        {
            Fail(line, "expected 'const <type> <literal>'");
        }

        instruction.Op = Instruction::Kind::Const;
        instruction.Ty = ParseTypeOrFail(tokens[0], pointerBits, line);
        instruction.Literal = tokens[1];
        return instruction;
    }

    if (op == "cast")
    {
        // cast <tipo> <valor>, <tipo origem>
        const std::size_t comma = text.find(',');
        if (comma == std::string::npos)
        {
            Fail(line, "expected 'cast <type> <value>, <source type>'");
        }

        std::vector<std::string> head = SplitWhitespace(text.substr(0, comma));
        if (head.size() != 2)
        {
            Fail(line, "expected 'cast <type> <value>, <source type>'");
        }

        instruction.Op = Instruction::Kind::Cast;
        instruction.Ty = ParseTypeOrFail(head[0], pointerBits, line);
        instruction.A = head[1];
        instruction.SourceTy = ParseTypeOrFail(Trim(text.substr(comma + 1)), pointerBits, line);
        return instruction;
    }

    if (op == "addr")
    {
        std::vector<std::string> tokens = SplitWhitespace(text);
        if (tokens.size() != 1)
        {
            Fail(line, "expected 'addr <name>'");
        }

        instruction.Op = Instruction::Kind::Addr;
        instruction.A = tokens[0];
        return instruction;
    }

    if (op == "load")
    {
        std::vector<std::string> tokens = SplitWhitespace(text);
        if (tokens.size() != 2)
        {
            Fail(line, "expected 'load <type> <pointer>'");
        }

        instruction.Op = Instruction::Kind::Load;
        instruction.Ty = ParseTypeOrFail(tokens[0], pointerBits, line);
        instruction.A = tokens[1];
        return instruction;
    }

    if (op == "call")
    {
        instruction.Op = Instruction::Kind::Call;
        ParseCallHeader(text, instruction, line, pointerBits);
        return instruction;
    }

    static const std::unordered_map<std::string, BinaryOp> binaries = {
        { "add", BinaryOp::Add }, { "sub", BinaryOp::Sub }, { "mul", BinaryOp::Mul },
        { "div", BinaryOp::Div }, { "rem", BinaryOp::Rem }, { "and", BinaryOp::And },
        { "or", BinaryOp::Or },   { "xor", BinaryOp::Xor }, { "shl", BinaryOp::Shl },
        { "shr", BinaryOp::Shr }, { "sar", BinaryOp::Sar }
    };

    if (binaries.contains(op))
    {
        // add <tipo> <a>, <b>
        const std::size_t comma = text.find(',');
        if (comma == std::string::npos)
        {
            Fail(line, "expected '" + op + " <type> <a>, <b>'");
        }

        std::vector<std::string> head = SplitWhitespace(text.substr(0, comma));
        if (head.size() != 2)
        {
            Fail(line, "expected '" + op + " <type> <a>, <b>'");
        }

        instruction.Op = Instruction::Kind::Binary;
        instruction.Binary = ParseBinaryOp(op, line);
        instruction.Ty = ParseTypeOrFail(head[0], pointerBits, line);
        instruction.A = head[1];
        instruction.B = Trim(text.substr(comma + 1));
        return instruction;
    }

    if (op == "neg" || op == "not" || op == "lnot" || op == "lnot32")
    {
        std::vector<std::string> tokens = SplitWhitespace(text);
        if (tokens.size() != 2)
        {
            Fail(line, "expected '" + op + " <type> <value>'");
        }

        instruction.Op = Instruction::Kind::Unary;
        instruction.Unary = ParseUnaryOp(op, line);
        instruction.Ty = ParseTypeOrFail(tokens[0], pointerBits, line);
        instruction.A = tokens[1];
        return instruction;
    }

    Fail(line, "unknown instruction '" + op + "'");
}

Instruction ParseInstruction(
    std::string_view line,
    std::size_t lineNumber,
    std::uint32_t pointerBits
)
{
    std::string text = Trim(line);

    // %tN = ...
    if (text.starts_with('%'))
    {
        const std::size_t equals = text.find('=');
        if (equals == std::string::npos)
        {
            Fail(lineNumber, "expected '%temp = <instruction>'");
        }

        std::string dst = Trim(text.substr(1, equals - 1));
        if (dst.empty())
        {
            Fail(lineNumber, "empty temporary name");
        }

        Instruction instruction = ParseValueInstruction(
            text.substr(equals + 1),
            lineNumber,
            pointerBits
        );
        instruction.Dst = dst;
        return instruction;
    }

    std::string rest = text;
    std::string op = FirstWord(rest);

    if (op == "storev")
    {
        // storev <tipo> <nome>, <valor>
        const std::size_t comma = text.find(',');
        if (comma == std::string::npos)
        {
            Fail(lineNumber, "expected 'storev <type> <name>, <value>'");
        }

        std::vector<std::string> head = SplitWhitespace(text.substr(0, comma));
        if (head.size() != 3)
        {
            Fail(lineNumber, "expected 'storev <type> <name>, <value>'");
        }

        Instruction instruction;
        instruction.Op = Instruction::Kind::StoreVar;
        instruction.Ty = ParseTypeOrFail(head[1], pointerBits, lineNumber);
        instruction.A = head[2];
        instruction.B = Trim(text.substr(comma + 1));
        return instruction;
    }

    if (op == "store")
    {
        // store <tipo> <ponteiro>, <valor>
        const std::size_t comma = text.find(',');
        if (comma == std::string::npos)
        {
            Fail(lineNumber, "expected 'store <type> <pointer>, <value>'");
        }

        std::vector<std::string> head = SplitWhitespace(text.substr(0, comma));
        if (head.size() != 3)
        {
            Fail(lineNumber, "expected 'store <type> <pointer>, <value>'");
        }

        Instruction instruction;
        instruction.Op = Instruction::Kind::Store;
        instruction.Ty = ParseTypeOrFail(head[1], pointerBits, lineNumber);
        instruction.A = head[2];
        instruction.B = Trim(text.substr(comma + 1));
        return instruction;
    }

    if (op == "ret")
    {
        Instruction instruction;
        instruction.Op = Instruction::Kind::Return;

        std::vector<std::string> tokens = SplitWhitespace(rest);
        if (!tokens.empty() && tokens[0] != "void")
        {
            if (tokens.size() != 2)
            {
                Fail(lineNumber, "expected 'ret <type> <value>' or 'ret'");
            }

            instruction.Ty = ParseTypeOrFail(tokens[0], pointerBits, lineNumber);
            instruction.A = tokens[1];
        }

        return instruction;
    }

    if (op == "call")
    {
        Instruction instruction;
        instruction.Op = Instruction::Kind::Call;
        ParseCallHeader(rest, instruction, lineNumber, pointerBits);
        return instruction;
    }

    if (op == "asm")
    {
        Instruction instruction;
        instruction.Op = Instruction::Kind::Asm;
        instruction.Literal = Trim(text.substr(3));
        return instruction;
    }

    Fail(lineNumber, "unknown directive '" + op + "'");
}

DataBlock ParseData(std::string_view text, std::size_t line)
{
    std::string rest = Trim(text);
    std::string op = FirstWord(rest);

    if (op != "data" && op != "bytes")
    {
        Fail(line, "expected 'data <label> <bytes>'");
    }

    std::string label = FirstWord(rest);
    if (label.empty())
    {
        Fail(line, "data without label");
    }

    DataBlock block;
    block.Label = label;
    block.Bytes = SplitTopLevel(rest, ',');
    return block;
}

Import ParseImport(std::string_view text, std::size_t line)
{
    std::vector<std::string> tokens = SplitWhitespace(text);
    if (tokens.size() < 4 || tokens[0] != "import")
    {
        Fail(line, "expected 'import <name> <symbol> <abi> <stackbytes>'");
    }

    Import import;
    import.Name = tokens[1];
    import.Symbol = tokens[2];
    import.Convention = AbiFromString(tokens[3]).value_or(Abi::Cdecl);
    import.StackBytes = tokens.size() >= 5
        ? static_cast<std::size_t>(std::stoull(tokens[4]))
        : 0;
    return import;
}

} // namespace

Module Parse(std::string_view text, std::uint32_t pointerBits)
{
    Module module;
    std::istringstream stream{ std::string(text) };
    std::string rawLine;
    std::size_t lineNumber = 0;

    while (std::getline(stream, rawLine))
    {
        ++lineNumber;
        std::string line = Trim(rawLine);

        if (line.empty() || line.starts_with(';') || line.starts_with('#'))
        {
            continue;
        }

        std::string rest = line;
        std::string op = FirstWord(rest);

        if (op == "module")
        {
            module.Name = Trim(rest);
            continue;
        }

        if (op == "import")
        {
            module.Imports.push_back(ParseImport(line, lineNumber));
            continue;
        }

        if (op == "data" || op == "bytes")
        {
            module.Data.push_back(ParseData(line, lineNumber));
            continue;
        }

        if (op == "global" || op == "extern")
        {
            auto [name, typeText] = SplitNameType(rest, lineNumber);
            Global global{ name, ParseTypeOrFail(typeText, pointerBits, lineNumber) };

            if (op == "global")
            {
                module.Globals.push_back(std::move(global));
            }
            else
            {
                module.Externals.push_back(std::move(global));
            }
            continue;
        }

        if (op == "export")
        {
            std::vector<std::string> tokens = SplitWhitespace(rest);
            if (tokens.size() != 2)
            {
                Fail(lineNumber, "expected 'export <name> <alias>'");
            }

            module.Exports.push_back({ tokens[0], tokens[1] });
            continue;
        }

        if (op == "func")
        {
            Function function;

            // func <nome> : <ret> <abi> <stackbytes>
            auto [name, tail] = SplitNameType(rest, lineNumber);
            function.Name = name;

            std::vector<std::string> tokens = SplitWhitespace(tail);
            if (tokens.empty())
            {
                Fail(lineNumber, "func without return type");
            }

            function.Ret = ParseTypeOrFail(tokens[0], pointerBits, lineNumber);

            if (tokens.size() >= 2)
            {
                function.Convention = AbiFromString(tokens[1]).value_or(Abi::Cdecl);
            }
            if (tokens.size() >= 3)
            {
                function.StackBytes = static_cast<std::size_t>(std::stoull(tokens[2]));
            }

            bool closed = false;

            while (std::getline(stream, rawLine))
            {
                ++lineNumber;
                std::string bodyLine = Trim(rawLine);

                if (bodyLine.empty() || bodyLine.starts_with(';') || bodyLine.starts_with('#'))
                {
                    continue;
                }

                if (bodyLine == "end")
                {
                    closed = true;
                    break;
                }

                if (bodyLine.starts_with("param "))
                {
                    auto [paramName, typeText] = SplitNameType(bodyLine.substr(6), lineNumber);
                    function.Params.push_back({
                        paramName,
                        ParseTypeOrFail(typeText, pointerBits, lineNumber)
                    });
                    continue;
                }

                if (bodyLine.starts_with("import "))
                {
                    module.Imports.push_back(ParseImport(bodyLine, lineNumber));
                    continue;
                }

                if (bodyLine.starts_with("data ") || bodyLine.starts_with("bytes "))
                {
                    module.Data.push_back(ParseData(bodyLine, lineNumber));
                    continue;
                }

                function.Body.push_back(
                    ParseInstruction(bodyLine, lineNumber, pointerBits)
                );
            }

            if (!closed)
            {
                Fail(lineNumber, "function '" + function.Name + "' without 'end'");
            }

            module.Functions.push_back(std::move(function));
            continue;
        }

        if (op == "asm")
        {
            // Assemblagem crua no nível de módulo não tem secção associada.
            Fail(lineNumber, "'asm' is only valid inside a function");
        }

        Fail(lineNumber, "unknown directive '" + op + "'");
    }

    return module;
}

} // namespace Emux::Exir
