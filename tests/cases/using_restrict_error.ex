[Main]

using Counter = u32
using Offset = u32

var offset : Offset

func Bump(Counter value) Counter {
    return value
}

func Start() i32 {
    offset = 40
    return Bump(offset)
}
