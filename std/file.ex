# API de ficheiros para Emux, por cima do kernel32.
# FILE e um handle opaco com a largura da plataforma (usize). As funcoes
# recebem caminhos como u8*, pelo que literais e blocos b sao aceites.

@ifndef EMUX_STD_FILE
@define EMUX_STD_FILE

@include "windows.ex"

[std]

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

func file_read(FILE handle, LPVOID buffer, u32 length) i32 {
    local bytes_read : u32 = 0
    return windows::ReadFile(handle, buffer, length, &bytes_read, 0)
}

func file_write(FILE handle, LPCVOID buffer, DWORD length) i32 {
    local bytes_written : u32 = 0
    return windows::WriteFile(handle, buffer, length, &bytes_written, 0)
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
func file_size(FILE handle) u32 {
    return windows::GetFileSize(handle, 0)
}

func size(FILE handle) u32 {
    return file_size(handle)
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

func file_remove(u8* path) i32 {
    return remove(path)
}

func rename(u8* from, u8* to) i32 {
    return windows::MoveFileA(from, to)
}

func file_copy(u8* from, u8* to, i32 failIfExists) i32 {
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

# Utilitarios de alto nivel

# Verifica se o ficheiro ou diretorio existe no disco
func exists(u8* path) bool {
    local attr : u32 = attributes(path)
    return attr != 0xFFFFFFFF
}

func file_exists(u8* path) bool {
    return exists(path)
}

# Verifica se o caminho aponta para um diretorio existente (FILE_ATTRIBUTE_DIRECTORY = 0x10)
func is_directory(u8* path) bool {
    local attr : u32 = attributes(path)
    if (attr == 0xFFFFFFFF) {
        return 0
    }
    return (attr & 16) != 0
}

# Retorna o tamanho em bytes de um ficheiro a partir do seu caminho
func size_of_path(u8* path) u32 {
    local h : FILE = open(path)
    if (h == 0 || h == 0xFFFFFFFFFFFFFFFF) {
        return 0
    }
    local s : u32 = file_size(h)
    close(h)
    return s
}

# Le todo o conteudo de um ficheiro para um buffer fornecido
func read_all(u8* path, u8* buffer, u32 max_bytes) u32 {
    local h : FILE = open(path)
    if (h == 0 || h == 0xFFFFFFFFFFFFFFFF) {
        return 0
    }

    local bytes_read : u32 = 0
    local success : i32 = windows::ReadFile(h, buffer, max_bytes, &bytes_read, 0)
    close(h)

    if (success == 0) {
        return 0
    }
    return bytes_read
}

# Grava 'bytes' no ficheiro indicado substituindo qualquer conteudo anterior
func write_all(u8* path, u8* data, u32 bytes) bool {
    local h : FILE = create(path)
    if (h == 0 || h == 0xFFFFFFFFFFFFFFFF) {
        return 0
    }

    local bytes_written : u32 = 0
    local success : i32 = windows::WriteFile(h, data, bytes, &bytes_written, 0)
    close(h)

    return success != 0 && bytes_written == bytes
}

@endif
