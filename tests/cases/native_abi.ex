[Main]

[[import("kernel32")]]
[[stdcall]]
global Sleep(u32 milliseconds) void

[[import("kernel32")]]
[[stdcall]]
global GetLastError() u32

global Start() i32 {
    Sleep(0)
    GetLastError()
    return 42
}
