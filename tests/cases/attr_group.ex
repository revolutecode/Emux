[Main]

[[import("kernel32")],[stdcall]]
func Sleep(u32 milliseconds) void

[[fastcall][noexcept]]
func Add(i32 a, i32 b) i32 {
    return a + b
}

[[cdecl],[export("emux_entry_point")]]
func Start() i32 {
    Sleep(0)
    return Add(20, 22)
}
