#include "Emux/Exir/ExirOptimizer.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Emux::Exir
{

namespace
{

std::vector<std::string> SplitForComma(const std::string& text);

// Um nome de temporario e '%' seguido de letras, digitos ou underscore.
bool IsTempChar(char c)
{
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// Recolhe, por ordem, todos os temporarios "%id" num fragmento de linha.
std::vector<std::string> CollectTemps(const std::string& text)
{
    std::vector<std::string> temps;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '%')
        {
            continue;
        }

        std::size_t j = i + 1;
        while (j < text.size() && IsTempChar(text[j]))
        {
            ++j;
        }

        if (j > i + 1)
        {
            temps.push_back(text.substr(i, j - i));
            i = j - 1;
        }
    }

    return temps;
}

std::string Trim(const std::string& text)
{
    std::size_t begin = 0;
    while (begin < text.size() &&
        std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }

    std::size_t end = text.size();
    while (end > begin &&
        std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }

    return text.substr(begin, end - begin);
}

std::string FirstWord(const std::string& text)
{
    std::size_t i = 0;
    while (i < text.size() &&
        !std::isspace(static_cast<unsigned char>(text[i])))
    {
        ++i;
    }

    return text.substr(0, i);
}

// Instrucoes sem efeitos colaterais: produzem apenas o seu destino. Podem ser
// eliminadas quando esse destino nunca e lido. Todo o resto (store, storev,
// call, ret, asm, copy e as diretorivas param/global/...) e conservado.
bool IsPureKeyword(const std::string& keyword)
{
    static const std::set<std::string> pure = {
        "const", "cast", "bitcast", "trunc", "addr", "load",
        "cmpmem",
        "add", "sub", "mul", "div", "rem",
        "and", "or", "xor", "shl", "shr", "sar",
        "eq", "ne", "lt", "gt", "le", "ge",
        "neg", "not", "lnot"
    };

    return pure.count(keyword) != 0;
}

// Uma linha de definicao tem a forma "%d = <keyword> ...". Devolve o temporario
// definido e a keyword; "rest" recebe o texto depois do '=' (a regiao operandos).
bool ParseDefLine(const std::string& line, std::string& def, std::string& keyword,
    std::string& operands)
{
    def.clear();
    keyword.clear();
    operands.clear();

    const std::string head = Trim(line);
    if (head.empty() || head[0] != '%')
    {
        return false;
    }

    const std::size_t equals = head.find('=');
    if (equals == std::string::npos)
    {
        return false;
    }

    const std::vector<std::string> leftTemps = CollectTemps(head.substr(0, equals));
    if (leftTemps.size() != 1 ||
        Trim(head.substr(0, equals)) != leftTemps[0])
    {
        return false;
    }

    def = leftTemps[0];
    operands = Trim(head.substr(equals + 1));
    keyword = FirstWord(operands);
    return true;
}

// Substitui cada temporario numa linha pela sua canonical, resolvendo a cadeia
// de aliases. So toca em "%id"; nomes de parametros/globais ficam intactos.
std::string RewriteTemps(const std::string& line,
    const std::map<std::string, std::string>& alias)
{
    if (alias.empty())
    {
        return line;
    }

    std::string out;
    out.reserve(line.size());

    for (std::size_t i = 0; i < line.size(); )
    {
        if (line[i] == '%')
        {
            std::size_t j = i + 1;
            while (j < line.size() && IsTempChar(line[j]))
            {
                ++j;
            }

            const std::string temp = line.substr(i, j - i);
            const auto it = alias.find(temp);
            out += (it != alias.end()) ? it->second : temp;
            i = j;
            continue;
        }

        out += line[i++];
    }

    return out;
}

std::string ResolveAlias(const std::map<std::string, std::string>& alias,
    const std::string& temp)
{
    std::string current = temp;
    std::set<std::string> seen;

    while (true)
    {
        const auto it = alias.find(current);
        if (it == alias.end() || !seen.insert(current).second)
        {
            return current;
        }

        current = it->second;
    }
}

// Elimina casts/bitcasts de identidade (mesmo tipo de origem e destino) por
// encaminhamento direto do operando para os consumidores do destino.
void EliminateIdentityCasts(std::vector<std::string>& body)
{
    std::map<std::string, std::string> alias;

    for (const std::string& line : body)
    {
        std::string def;
        std::string keyword;
        std::string operands;

        if (!ParseDefLine(line, def, keyword, operands))
        {
            continue;
        }

        if (keyword != "cast" && keyword != "bitcast")
        {
            continue;
        }

        // Formato: cast <tipo destino> <valor>, <tipo origem>
        const std::size_t comma = operands.find(',');
        if (comma == std::string::npos)
        {
            continue;
        }

        std::vector<std::string> head =
            SplitForComma(operands.substr(0, comma));
        const std::string sourceType = Trim(operands.substr(comma + 1));

        // head = ["cast", tipo, valor]
        if (head.size() != 3 || head[2].empty())
        {
            continue;
        }

        if (head[1] != sourceType)
        {
            continue;
        }

        const std::string value = ResolveAlias(alias, head[2]);
        alias[def] = value;
    }

    if (alias.empty())
    {
        return;
    }

    std::vector<std::string> out;
    out.reserve(body.size());

    for (const std::string& line : body)
    {
        std::string def;
        std::string keyword;
        std::string operands;

        if (ParseDefLine(line, def, keyword, operands) &&
            alias.count(def) != 0 &&
            (keyword == "cast" || keyword == "bitcast"))
        {
            // Linha de cast identidade descartada; o destino ja foi reescrito.
            continue;
        }

        out.push_back(RewriteTemps(line, alias));
    }

    body.swap(out);
}

std::vector<std::string> SplitForComma(const std::string& text)
{
    std::vector<std::string> tokens;
    std::istringstream stream{text};
    std::string token;

    while (stream >> token)
    {
        tokens.push_back(token);
    }

    return tokens;
}

// DCE: remove linhas puras cujo destino nunca e lido por uma linha sobrevivente,
// ate a ponto fixo (a morte de um consumidor pode matar os seus produtores).
void DeadCodeElimination(std::vector<std::string>& body)
{
    std::vector<std::string> def(body.size());
    std::vector<std::string> keyword(body.size());
    std::vector<char> isDef(body.size(), 0);
    std::vector<char> kept(body.size(), 1);

    for (std::size_t i = 0; i < body.size(); ++i)
    {
        std::string operands;
        isDef[i] = ParseDefLine(body[i], def[i], keyword[i], operands) ? 1 : 0;
    }

    bool changed = true;

    while (changed)
    {
        changed = false;

        std::unordered_map<std::string, int> useCount;

        for (std::size_t i = 0; i < body.size(); ++i)
        {
            if (!kept[i])
            {
                continue;
            }

            // Contam-se apenas os usos nos operandos; o definidor em si nao e
            // um uso (para linhas de definicao) ou todo o resto conta.
            const std::vector<std::string> temps = CollectTemps(body[i]);

            for (const std::string& temp : temps)
            {
                if (isDef[i] && temp == def[i])
                {
                    continue;
                }

                ++useCount[temp];
            }
        }

        for (std::size_t i = 0; i < body.size(); ++i)
        {
            if (!kept[i] || !isDef[i] || !IsPureKeyword(keyword[i]))
            {
                continue;
            }

            if (useCount.count(def[i]) == 0)
            {
                kept[i] = 0;
                changed = true;
            }
        }
    }

    std::vector<std::string> out;
    out.reserve(body.size());

    for (std::size_t i = 0; i < body.size(); ++i)
    {
        if (kept[i])
        {
            out.push_back(body[i]);
        }
    }

    body.swap(out);
}

} // namespace

std::string Optimize(std::string_view irText, OptimizationLevel level)
{
    if (level == OptimizationLevel::O0)
    {
        return std::string(irText);
    }

    std::istringstream in{ std::string(irText) };
    std::ostringstream out;
    std::string line;

    std::vector<std::string> body;
    bool inFunction = false;

    auto flushBody = [&]()
    {
        if (level >= OptimizationLevel::O2)
        {
            EliminateIdentityCasts(body);
        }

        DeadCodeElimination(body);

        for (const std::string& bodyLine : body)
        {
            out << bodyLine << '\n';
        }

        body.clear();
    };

    while (std::getline(in, line))
    {
        // Preserva o conteudo para analise; a saida usa sempre '\\n'.
        std::string content = line;
        if (!content.empty() && content.back() == '\r')
        {
            content.pop_back();
        }

        const std::string trimmed = Trim(content);

        if (!inFunction)
        {
            if (trimmed.rfind("func", 0) == 0)
            {
                inFunction = true;
            }

            out << content << '\n';
            continue;
        }

        if (trimmed == "end")
        {
            // Todo o corpo (instrucoes e diretorivas param/import/data/bytes)
            // fica no buffer por ordem; as diretorivas nao sao linhas de
            // definicao, portanto DCE e o peephole de casts preservam-nas.
            flushBody();
            inFunction = false;
            out << content << '\n';
            continue;
        }

        body.push_back(content);
    }

    // Func sem 'end' (IR malformada): despeja o corpo remanescente tal como veio.
    if (inFunction)
    {
        flushBody();
    }

    return out.str();
}

} // namespace Emux::Exir
