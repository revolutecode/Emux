[Main]

[[import("kernel32")]]
global Sleep(u32 milliseconds) void

global Start() i32 {
    Sleep(0)
    return 42
}
