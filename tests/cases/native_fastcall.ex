[Main]

[[import("kernel32")]]
[[fastcall]]
global GetTickCount() u32

global Start() i32 {
    GetTickCount()
    return 42
}
