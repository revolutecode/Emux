# API de ficheiros para Emux, por cima do kernel32.
# FILE e um handle opaco com a largura da plataforma (usize). As funcoes
# recebem caminhos como u8*, pelo que literais e blocos b sao aceites.

@ifndef EMUX_STL_FILE
@define EMUX_STL_FILE

@include "windows.ex"

[file]

using FILE = usize

# Acesso pedido ao CreateFileA.
[[const]] var AccessRead : u32 = 0x80000000
[[const]] var AccessWrite : u32 = 0x40000000
[[const]] var AccessReadWrite : u32 = 0xC0000000
[[const]] var AccessAppend : u32 = 0x00000004

# Modo de criacao.
[[const]] var CreateNew : u32 = 1
[[const]] var CreateAlways : u32 = 2
[[const]] var OpenExisting : u32 = 3
[[const]] var OpenAlways : u32 = 4

# Origem do SetFilePointer.
[[const]] var SeekBegin : u32 = 0
[[const]] var SeekCurrent : u32 = 1
[[const]] var SeekEnd : u32 = 2

# Abre um ficheiro existente para leitura.
func open(u8* path) FILE {
    return windows::CreateFileA(path, AccessRead, SeekCurrent, 0, OpenExisting, 0x80, 0)
}

# Abre (ou cria) para leitura e escrita sem truncar.
func open_read_write(u8* path) FILE {
    return windows::CreateFileA(path, 0xC0000000, 1, 0, 4, 0x80, 0)
}

# Cria um ficheiro novo (trunca se existir) para escrita.
func create(u8* path) FILE {
    return windows::CreateFileA(path, 0x40000000, 0, 0, 2, 0x80, 0)
}

# Cria um ficheiro novo e falha se ja existir.
func create_new(u8* path) FILE {
    return windows::CreateFileA(path, 0x40000000, 0, 0, 1, 0x80, 0)
}

# Abre para escrita, criando se preciso, e acrescenta no fim.
func append(u8* path) FILE {
    return windows::CreateFileA(path, 0x00000004, 1, 0, 4, 0x80, 0)
}

func read(FILE handle, LPVOID buffer, u32 length) i32 {
    return windows::ReadFile(handle, buffer, length, 0, 0)
}

func write(FILE handle, LPCVOID buffer, u32 length) i32 {
    return windows::WriteFile(handle, buffer, length, 0, 0)
}

# Escreve uma string terminada em NUL.
func write_text(FILE handle, u8* text) i32 {
    return windows::WriteFile(handle, text, windows::lstrlenA(text), 0, 0)
}

func close(FILE handle) i32 {
    return windows::CloseHandle(handle)
}

func flush(FILE handle) i32 {
    return windows::FlushFileBuffers(handle)
}

# Tamanho do ficheiro em bytes (limite de 4 GiB; usa-se o DWORD baixo).
func size(FILE handle) u32 {
    return windows::GetFileSize(handle, 0)
}

func seek(FILE handle, u32 offset, u32 origin) u32 {
    return windows::SetFilePointer(handle, offset, 0, origin)
}

func seek_begin(FILE handle, u32 offset) u32 {
    return windows::SetFilePointer(handle, offset, 0, 0)
}

func seek_end(FILE handle, u32 offset) u32 {
    return windows::SetFilePointer(handle, offset, 0, 2)
}

# Trunca no ponto corrente (combina com seek_begin/seek_end).
func truncate(FILE handle) i32 {
    return windows::SetEndOfFile(handle)
}

func remove(u8* path) i32 {
    return windows::DeleteFileA(path)
}

func rename(u8* from, u8* to) i32 {
    return windows::MoveFileA(from, to)
}

func copy(u8* from, u8* to, i32 failIfExists) i32 {
    return windows::CopyFileA(from, to, failIfExists)
}

# 0xFFFFFFFF (INVALID_FILE_ATTRIBUTES) indica caminho inexistente.
func attributes(u8* path) u32 {
    return windows::GetFileAttributesA(path)
}

func set_attributes(u8* path, u32 attributes) i32 {
    return windows::SetFileAttributesA(path, attributes)
}

func make_directory(u8* path) i32 {
    return windows::CreateDirectoryA(path, 0)
}

func remove_directory(u8* path) i32 {
    return windows::RemoveDirectoryA(path)
}

@endif
