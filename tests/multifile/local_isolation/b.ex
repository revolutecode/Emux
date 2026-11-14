[Main]

[[local]]
func Helper() i32 {
    return 32
}

func Start() i32 {
    return GetA() + Helper()
}
