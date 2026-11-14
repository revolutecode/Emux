# STD base do Windows para Emux.
# As funcoes nativas sao declaradas com [[import("kernel32")]], que valida
# nome, biblioteca e ABI em vez de escrever 'import' cru no exir.

@ifndef EMUX_STD_WINDOWS
@define EMUX_STD_WINDOWS

@define BOOL i32
@define DWORD u32
@define LPDWORD u32*
@define HANDLE usize
@define LPVOID u8*
@define LPCVOID u8*
@define POINTER usize

[windows]

[[import("kernel32")]]
[[stdcall]]
global GetStdHandle(u32 which) HANDLE

[[import("kernel32")]]
[[stdcall]]
global ReadFile(HANDLE file, LPVOID buffer, DWORD length, LPDWORD read, LPVOID overlapped) BOOL

[[import("kernel32")]]
[[stdcall]]
global WriteFile(HANDLE file, LPCVOID buffer, DWORD length, LPDWORD written, LPVOID overlapped) BOOL

[[import("kernel32")]]
[[stdcall]]
global Sleep(DWORD milliseconds) void

[[import("kernel32")]]
[[stdcall]]
global GetLastError() DWORD

[[import("kernel32")]]
[[stdcall]]
global GetTickCount() DWORD

[[import("kernel32")]]
[[stdcall]]
global GetCommandLineA() LPCVOID

[[import("kernel32")]]
[[stdcall]]
global lstrlenA(LPCVOID text) i32

[[import("kernel32")]]
[[stdcall]]
global CreateFileA(LPCVOID path, DWORD access, DWORD share, LPVOID security, DWORD creation, DWORD flags, HANDLE template) HANDLE

[[import("kernel32")]]
[[stdcall]]
global CloseHandle(HANDLE handle) BOOL

[[import("kernel32")]]
[[stdcall]]
global FlushFileBuffers(HANDLE file) BOOL

[[import("kernel32")]]
[[stdcall]]
global SetFilePointer(HANDLE file, u32 distance, LPVOID high, u32 method) DWORD

[[import("kernel32")]]
[[stdcall]]
global GetFileSize(HANDLE file, LPVOID high) DWORD

[[import("kernel32")]]
[[stdcall]]
global SetEndOfFile(HANDLE file) BOOL

[[import("kernel32")]]
[[stdcall]]
global DeleteFileA(LPCVOID path) BOOL

[[import("kernel32")]]
[[stdcall]]
global MoveFileA(LPCVOID from, LPCVOID to) BOOL

[[import("kernel32")]]
[[stdcall]]
global CopyFileA(LPCVOID from, LPCVOID to, BOOL failIfExists) BOOL

[[import("kernel32")]]
[[stdcall]]
global GetFileAttributesA(LPCVOID path) DWORD

[[import("kernel32")]]
[[stdcall]]
global SetFileAttributesA(LPCVOID path, DWORD attributes) BOOL

[[import("kernel32")]]
[[stdcall]]
global CreateDirectoryA(LPCVOID path, LPVOID security) BOOL

[[import("kernel32")]]
[[stdcall]]
global RemoveDirectoryA(LPCVOID path) BOOL

[[import("kernel32")]]
[[stdcall]]
global ReadConsoleA(HANDLE input, LPVOID buffer, DWORD length, LPDWORD read, LPVOID reserved) BOOL

[[import("kernel32")]]
[[stdcall]]
global WriteConsoleA(HANDLE output, LPCVOID buffer, DWORD length, LPDWORD written, LPVOID reserved) BOOL

[[import("kernel32")]]
[[stdcall]]
global GetProcessHeap() HANDLE

[[import("kernel32")]]
[[stdcall]]
global HeapAlloc(HANDLE heap, DWORD flags, usize bytes) LPVOID

[[import("kernel32")]]
[[stdcall]]
global HeapFree(HANDLE heap, DWORD flags, LPVOID mem) BOOL

[[import("kernel32")]]
[[stdcall]]
global ExitProcess(u32 exitCode) void

# Wrappers de baixo nivel. Nao ha variaveis locais: cada wrapper encadeia
# chamadas numa unica expressao.

global newfile(LPCVOID path) HANDLE {
    return CreateFileA(path, 0x80000000, 0, 0, 1, 0x80, 0)
}

global write(HANDLE file, LPCVOID buffer, DWORD length) BOOL {
    local dummy : u32 = 0
    return WriteFile(file, buffer, length, &dummy, 0)
}

global read(HANDLE file, LPVOID buffer, DWORD length) BOOL {
    local dummy : u32 = 0
    return ReadFile(file, buffer, length, &dummy, 0)
}

global console_out(LPCVOID buffer, DWORD length) BOOL {
    return write(GetStdHandle(-11), buffer, length)
}

global console_err(LPCVOID buffer, DWORD length) BOOL {
    return write(GetStdHandle(-12), buffer, length)
}

global console_in(LPVOID buffer, DWORD length) BOOL {
    return read(GetStdHandle(-10), buffer, length)
}

@endif
