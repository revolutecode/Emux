# IO de consola para Emux, por cima do kernel32.
# Inclui "windows.ex"; o guard de inclusao evita declaracoes duplicadas
# quando o programa inclui tambem "file.ex" ou "std.ex".

@ifndef EMUX_STL_IO
@define EMUX_STL_IO

@include "windows.ex"

[io]

[[const]] var Newline : B3 = "\r\n"

# Escreve uma string terminada em NUL no stdout.
func print(u8* text) i32 {
    return windows::console_out(text, windows::lstrlenA(text))
}

# Escreve a string e termina a linha.
func println(u8* text) i32 {
    windows::console_out(text, windows::lstrlenA(text))
    return windows::console_out(Newline, 2)
}

# Escreve uma string terminada em NUL no stderr.
func eprint(u8* text) i32 {
    return windows::console_err(text, windows::lstrlenA(text))
}

func eprintln(u8* text) i32 {
    windows::console_err(text, windows::lstrlenA(text))
    return windows::console_err(Newline, 2)
}

# Bytes crus, sem contar com o NUL.
func write(u8* text, u32 length) i32 {
    return windows::console_out(text, length)
}

# Le ate 'capacity' bytes do stdin para o buffer fornecido.
func read(LPVOID buffer, u32 capacity) i32 {
    return windows::console_in(buffer, capacity)
}

# Escreve bytes crus no stderr.
func write_err(u8* text, u32 length) i32 {
    return windows::console_err(text, length)
}

@endif
