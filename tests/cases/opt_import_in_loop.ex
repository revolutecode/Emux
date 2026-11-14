[Main]

global i : i32

[[import("kernel32")]]
global Sleep(u32 milliseconds) void

[[import("kernel32")]]
global GetTickCount() u32

global Start() i32 {
    i = 0
    while (i < 3) {
        Sleep(0)
        i = i + 1
    }
    return 42
}
