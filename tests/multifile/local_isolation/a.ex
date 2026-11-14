[Main]

[[local]]
func Helper() i32 {
    return 10
}

func GetA() i32 {
    return Helper()
}
