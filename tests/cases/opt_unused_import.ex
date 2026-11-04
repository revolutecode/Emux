[Main]

[[import("kernel32")]]
func Sleep(u32 milliseconds) void

[[import("kernel32")]]
func GetTickCount() u32

func Start() i32 {
    Sleep(0)
    return 42
}
