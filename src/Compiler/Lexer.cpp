#include <Emux/Compiler/Lexer.hpp>
#include <Emux/Compiler/Keywords.hpp>

#include <cctype>


namespace Emux
{


Lexer::Lexer(CompilerContext& context):
    m_Context(context)
{
}


std::vector<Token> Lexer::Tokenize()
{
    std::vector<Token> tokens;


    while(Current() != '\0')
    {
        tokens.push_back(
            ScanToken()
        );
    }


    tokens.push_back(
    {
        TokenType::EndOfFile,
        "",
        {
            m_Context.Source.GetName(),
            m_Line,
            m_Column
        }
    });

    m_Context.Tokens = std::move(tokens);
    return m_Context.Tokens;
}


char Lexer::Current() const
{
    return m_Context.Source[m_Position];
}

char Lexer::Peek() const
{
    if(m_Position + 1 >= m_Context.Source.Size())
    {
        return '\0';
    }


    return m_Context.Source[m_Position + 1];
}

char Lexer::PeekNext() const
{
    const auto& source = m_Context.Source;


    if(m_Position + 2 >= source.Size())
    {
        return '\0';
    }


    return source[m_Position + 2];
}

void Lexer::Advance()
{
    if (Current() == '\0') {
        return;
    }

    if(Current() == '\n')
    {
        m_Line++;
        m_Column = 1;
    }
    else
    {
        m_Column++;
    }


    m_Position++;
}


Token Lexer::ScanToken()
{
    SourceLocation location
    {
        .File = m_Context.Source.GetName(),
        .Line = m_Line,
        .Column = m_Column,
        .Offset = m_Position
    };

    // Comentário
    if(Current() == '#')
    {
        if(Peek() == '#' && PeekNext() == '#')
        {
            SkipBlockComment();
        }
        else
        {
            SkipLineComment();
        }


        return ScanToken();
    }

    if(Current() == '"')
    {
        return ScanString();
    }

    // Ignorar espaços
    char c = Current();
    if(c == ' ' || c == '\t')
    {
        Advance();

        return ScanToken();
    }


    // Nova linha
    if(c == '\n')
    {
        Advance();

        return
        {
            TokenType::NewLine,
            "\\n",
            location
        };
    }


    // Símbolos

    if (c  == '-' && Peek() == '>')
    {
        Advance();
        Advance();
        return
        {
            TokenType::Pointer,
            "->",
            location
        };
    }

    switch(c)
    {

        case '(':
            Advance();

            return
            {
                TokenType::LeftParen,
                "(",
                location
            };

        case ')':
            Advance();

            return
            {
                TokenType::RightParen,
                ")",
                location
            };

        case '[':
            Advance();

            return
            {
                TokenType::LeftBracket,
                "[",
                location
            };


        case ']':
            Advance();

            return
            {
                TokenType::RightBracket,
                "]",
                location
            };

        case '{':
            Advance();

            return
            {
                TokenType::LeftBrace,
                "{",
                location
            };


        case '}':
            Advance();

            return
            {
                TokenType::RightBrace,
                "}",
                location
            };


        case ':':
            Advance();

            return
            {
                TokenType::Colon,
                ":",
                location
            };


        case ',':
            Advance();

            return
            {
                TokenType::Comma,
                ",",
                location
            };


        case '=':
            Advance();

            return
            {
                TokenType::Assign,
                "=",
                location
            };

        case '+':
            Advance();

            return
            {
                TokenType::Plus,
                "+",
                location
            };

        case '-':
            Advance();

            return
            {
                TokenType::Minus,
                "-",
                location
            };

    }


    // Número

    if(std::isdigit(c) || (Current() == '0' && Peek() == 'x'))
    {
        return ScanNumber();
    }


    // Identificador

    if(std::isalpha(c) || c == '_')
    {
        return ScanIdentifierOrKeyword();
    }


    // Desconhecido

    Advance();

    return
    {
        TokenType::Unknown,
        std::string(1,c),
        location
    };
}


Token Lexer::ScanIdentifierOrKeyword()
{
    SourceLocation location
    {
        .File = m_Context.Source.GetName(),
        .Line = m_Line,
        .Column = m_Column,
        .Offset = m_Position
    };


    std::string text;


    while(
        std::isalnum(Current())
        ||
        Current() == '_'
    )
    {
        text += Current();

        Advance();
    }

    location.Length = text.size();
    TokenType type = TokenType::Identifier;

    for (const std::string& key : keywords) {
        if (text == key) type = TokenType::Keyword;
    }

    return
    {
        type,
        text,
        location
    };
}

Token Lexer::ScanNumber()
{
    SourceLocation location
    {
        .File = m_Context.Source.GetName(),
        .Line = m_Line,
        .Column = m_Column,
        .Offset = m_Position
    };


    std::string text;


    // hexadecimal

    if(Current() == '0' && Peek() == 'x')
    {
        text += Current();
        Advance();

        text += Current();
        Advance();


        while(
            std::isxdigit(Current())
        )
        {
            text += Current();

            Advance();
        }
    }
    else
    {

        while(
            std::isdigit(Current())
        )
        {
            text += Current();

            Advance();
        }

    }


    location.Length = text.size();


    return
    {
        TokenType::Number,
        text,
        location
    };
}

Token Lexer::ScanString()
{
    SourceLocation location
    {
        .File = m_Context.Source.GetName(),
        .Line = m_Line,
        .Column = m_Column,
        .Offset = m_Position
    };

    Advance();

    std::string text;

    while(
        Current() != '"'
        &&
        Current() != '\0'
    )
    {
        text += Current();

        Advance();
    }

    if(Current() == '"')
    {
        Advance();
    }

    location.Length = text.size() + 2;

    return
    {
        TokenType::String,
        text,
        location
    };
}

void Lexer::SkipLineComment()
{
    while(
        Current() != '\n'
        &&
        Current() != '\0'
    )
    {
        Advance();
    }
}

void Lexer::SkipBlockComment()
{

    // pula o primeiro ###

    Advance();
    Advance();
    Advance();


    while(Current() != '\0')
    {

        if(
            Current() == '#'
            &&
            Peek() == '#'
            &&
            PeekNext() == '#'
        )
        {
            Advance();
            Advance();
            Advance();

            return;
        }


        Advance();
    }

}

}
