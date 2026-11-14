# IO de consola para Emux, por cima do kernel32.
# Inclui "windows.ex"; o guard de inclusao evita declaracoes duplicadas
# quando o programa inclui tambem "file.ex" ou "std.ex".

@ifndef EMUX_STD_IO
@define EMUX_STD_IO

@include "windows.ex"

[std]

[[const]] global Newline : B3 = "\r\n"

# Escreve uma string terminada em NUL no stdout.
global print(u8* text) i32 {
    return windows::console_out(text, windows::lstrlenA(text))
}

# Escreve a string e termina a linha.
global println(u8* text) i32 {
    windows::console_out(text, windows::lstrlenA(text))
    return windows::console_out(Newline, 2)
}

# Escreve uma string terminada em NUL no stderr.
global eprint(u8* text) i32 {
    return windows::console_err(text, windows::lstrlenA(text))
}

global eprintln(u8* text) i32 {
    windows::console_err(text, windows::lstrlenA(text))
    return windows::console_err(Newline, 2)
}

# Bytes crus, sem contar com o NUL.
global console_write(u8* text, u32 length) i32 {
    return windows::console_out(text, length)
}

# Le ate 'capacity' bytes do stdin para o buffer fornecido.
global console_read(LPVOID buffer, u32 capacity) i32 {
    return windows::console_in(buffer, capacity)
}

# Escreve bytes crus no stderr.
global console_write_err(u8* text, u32 length) i32 {
    return windows::console_err(text, length)
}

# Escreve um unico caractere no stdout
global print_char(u8 c) void {
    local ch : u8 = c
    console_write(&ch, 1)
}

# Escreve um inteiro decimal no stdout
global print_int(i64 val) void {
    if (val == 0) {
        print_char(48) # '0'
    } else {
        local n : i64 = val
        if (val < 0) {
            print_char(45) # '-'
            n = -val
        }

        local temp : B32
        local count : usize = 0
        while (n > 0 && count < 30) {
            temp[count] = 48 + (n % 10)
            n = n / 10
            count = count + 1
        }

        while (count > 0) {
            count = count - 1
            print_char(temp[count])
        }
    }
}

# Escreve um inteiro decimal no stdout e salta de linha
global println_int(i64 val) void {
    print_int(val)
    windows::console_out(Newline, 2)
}

# Escreve um inteiro em formato hexadecimal (ex: 0x2A)
global print_hex(u64 val) void {
    print("0x")
    if (val == 0) {
        print_char(48) # '0'
    } else {
        local temp : B16
        local count : usize = 0
        local n : u64 = val

        while (n > 0 && count < 16) {
            local rem : u64 = n % 16
            if (rem < 10) {
                temp[count] = 48 + rem
            } else {
                temp[count] = 65 + (rem - 10) # 'A'..'F'
            }
            n = n / 16
            count = count + 1
        }

        while (count > 0) {
            count = count - 1
            print_char(temp[count])
        }
    }
}

# Escreve um hexadecimal e salta de linha
global println_hex(u64 val) void {
    print_hex(val)
    windows::console_out(Newline, 2)
}

@endif
