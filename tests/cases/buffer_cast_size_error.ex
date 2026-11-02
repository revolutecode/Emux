[Main]

var small : B2
var p : u32*

func Start() i32 {
    small = "A"
    p = static_cast<u32*>(&small)
    return 0
}
