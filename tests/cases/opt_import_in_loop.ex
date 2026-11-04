[Main]

var i : i32

[[import("kernel32")]]
func Sleep(u32 milliseconds) void

[[import("kernel32")]]
func GetTickCount() u32

func Start() i32 {
    i = 0
    while (i < 3) {
        Sleep(0)
        i = i + 1
    }
    return 42
}
