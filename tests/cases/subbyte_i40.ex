[Main]
var s : i40

func Start() i32 {
    s = 549755813887
    s = s + s
    s = s >> 39
    return static_cast<i32>(s & 255)
}
