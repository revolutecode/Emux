# SDK basico do Windows para Emux.
# As funcoes nativas sao declaradas com [[import("kernel32")]], que valida
# nome, biblioteca e ABI em vez de escrever 'import' cru no exir.

@define BOOL i32
@define DWORD u32
@define HANDLE u64
@define LPVOID u64
@define LPCVOID u64
@define POINTER u64

[windows]

[[import("kernel32")]]
func GetStdHandle(u32 which) HANDLE

[[import("kernel32")]]
func ReadFile(HANDLE file, LPVOID buffer, DWORD length, LPVOID read, LPVOID overlapped) BOOL

[[import("kernel32")]]
func WriteFile(HANDLE file, LPCVOID buffer, DWORD length, LPVOID written, LPVOID overlapped) BOOL

[[import("kernel32")]]
func Sleep(DWORD milliseconds) void

[[import("kernel32")]]
func GetLastError() DWORD

[[import("kernel32")]]
func GetTickCount() DWORD

[[import("kernel32")]]
func CreateFileA(LPCVOID path, DWORD access, DWORD share, LPVOID security, DWORD creation, DWORD flags, HANDLE template) HANDLE

# Wrappers Emux. Nao ha variaveis locais, por isso cada wrapper e uma
# expressao unica.

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
