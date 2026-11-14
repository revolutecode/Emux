[Main]

global small : B2
global p : u32*

global Start() i32 {
    small = "A"
    p = static_cast<u32*>(&small)
    return 0
}
