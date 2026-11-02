# STL base do Windows para Emux.
# As funcoes nativas sao declaradas com [[import("kernel32")]], que valida
# nome, biblioteca e ABI em vez de escrever 'import' cru no exir.

@ifndef EMUX_STL_WINDOWS
@define EMUX_STL_WINDOWS

@define BOOL i32
@define DWORD u32
@define HANDLE usize
@define LPVOID u8*
@define LPCVOID u8*
@define POINTER usize

[windows]

[[import("kernel32")]]
[[stdcall]]
func GetStdHandle(u32 which) HANDLE

[[import("kernel32")]]
[[stdcall]]
func ReadFile(HANDLE file, LPVOID buffer, DWORD length, LPVOID read, LPVOID overlapped) BOOL

[[import("kernel32")]]
[[stdcall]]
func WriteFile(HANDLE file, LPCVOID buffer, DWORD length, LPVOID written, LPVOID overlapped) BOOL

[[import("kernel32")]]
[[stdcall]]
func Sleep(DWORD milliseconds) void

[[import("kernel32")]]
[[stdcall]]
func GetLastError() DWORD

[[import("kernel32")]]
[[stdcall]]
func GetTickCount() DWORD

[[import("kernel32")]]
[[stdcall]]
func GetCommandLineA() LPCVOID

[[import("kernel32")]]
[[stdcall]]
func lstrlenA(LPCVOID text) i32

[[import("kernel32")]]
[[stdcall]]
func CreateFileA(LPCVOID path, DWORD access, DWORD share, LPVOID security, DWORD creation, DWORD flags, HANDLE template) HANDLE

[[import("kernel32")]]
[[stdcall]]
func CloseHandle(HANDLE handle) BOOL

[[import("kernel32")]]
[[stdcall]]
func FlushFileBuffers(HANDLE file) BOOL

[[import("kernel32")]]
[[stdcall]]
func SetFilePointer(HANDLE file, u32 distance, LPVOID high, u32 method) DWORD

[[import("kernel32")]]
[[stdcall]]
func GetFileSize(HANDLE file, LPVOID high) DWORD

[[import("kernel32")]]
[[stdcall]]
func SetEndOfFile(HANDLE file) BOOL

[[import("kernel32")]]
[[stdcall]]
func DeleteFileA(LPCVOID path) BOOL

[[import("kernel32")]]
[[stdcall]]
func MoveFileA(LPCVOID from, LPCVOID to) BOOL

[[import("kernel32")]]
[[stdcall]]
func CopyFileA(LPCVOID from, LPCVOID to, BOOL failIfExists) BOOL

[[import("kernel32")]]
[[stdcall]]
func GetFileAttributesA(LPCVOID path) DWORD

[[import("kernel32")]]
[[stdcall]]
func SetFileAttributesA(LPCVOID path, DWORD attributes) BOOL

[[import("kernel32")]]
[[stdcall]]
func CreateDirectoryA(LPCVOID path, LPVOID security) BOOL

[[import("kernel32")]]
[[stdcall]]
func RemoveDirectoryA(LPCVOID path) BOOL

[[import("kernel32")]]
[[stdcall]]
func ReadConsoleA(HANDLE input, LPVOID buffer, DWORD length, LPVOID read, LPVOID reserved) BOOL

[[import("kernel32")]]
[[stdcall]]
func WriteConsoleA(HANDLE output, LPCVOID buffer, DWORD length, LPVOID written, LPVOID reserved) BOOL

# Wrappers de baixo nivel. Nao ha variaveis locais: cada wrapper encadeia
# chamadas numa unica expressao.

func newfile(LPCVOID path) HANDLE {
    return CreateFileA(path, 0x80000000, 0, 0, 1, 0x80, 0)
}

func write(HANDLE file, LPCVOID buffer, DWORD length) BOOL {
    return WriteFile(file, buffer, length, 0, 0)
}

func read(HANDLE file, LPVOID buffer, DWORD length) BOOL {
    return ReadFile(file, buffer, length, 0, 0)
}

func console_out(LPCVOID buffer, DWORD length) BOOL {
    return write(GetStdHandle(-11), buffer, length)
}

func console_err(LPCVOID buffer, DWORD length) BOOL {
    return write(GetStdHandle(-12), buffer, length)
}

func console_in(LPVOID buffer, DWORD length) BOOL {
    return read(GetStdHandle(-10), buffer, length)
}

@endif
