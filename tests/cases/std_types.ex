[Main]

var count : usize
var offset : isize
var flag : bool

func Start() i32 {
    count = 40
    offset = 2
    flag = 1
    return count + offset + (1 - flag)
}
