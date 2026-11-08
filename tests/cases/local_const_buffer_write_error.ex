[Main]

func Start() i32 {
    [[const]] local msg : B4 = "ab"
    msg[0] = 67
    return msg[0]
}
