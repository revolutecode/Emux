[Main]

global value : i32
global address : uptr

global Start() i32 {
    value = 42
    address = reinterpret_cast<uptr>(&value)
    return static_cast<i32>(value)
}
