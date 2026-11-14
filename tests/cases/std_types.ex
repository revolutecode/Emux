[Main]

global count : usize
global offset : isize
global flag : bool

global Start() i32 {
    count = 40
    offset = 2
    flag = 1
    return count + offset + (1 - flag)
}
