[Main]

[[import("kernel32")]]
func Sleep(u32 milliseconds) void

func Start() i32 {
    Sleep(0)
    return 42
}
