[Main]

[[import("kernel32")]]
[[fastcall]]
func GetTickCount() u32

func Start() i32 {
    GetTickCount()
    return 42
}
