[Main]

[[import("kernel32")]]
[[stdcall]]
func Sleep(u32 milliseconds) void

[[import("kernel32")]]
[[stdcall]]
func GetLastError() u32

func Start() i32 {
    Sleep(0)
    GetLastError()
    return 42
}
