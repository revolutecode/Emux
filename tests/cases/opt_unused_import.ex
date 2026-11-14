[Main]

[[import("kernel32")]]
global Sleep(u32 milliseconds) void

[[import("kernel32")]]
global GetTickCount() u32

global Start() i32 {
    Sleep(0)
    return 42
}
