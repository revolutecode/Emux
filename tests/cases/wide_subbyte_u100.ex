[Main]
global a : u100

global Start() i32 {
    a = 1267650600228229401496703205375
    a = a + 1
    return static_cast<i32>(a >> 99)
}
