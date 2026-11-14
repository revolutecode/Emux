[Main]

using Counter = u32
using Offset = u32

global offset : Offset

global Bump(Counter value) Counter {
    return value
}

global Start() i32 {
    offset = 40
    return Bump(offset)
}
