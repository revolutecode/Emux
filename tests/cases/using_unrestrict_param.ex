[Main]

using Counter = u32
using Offset = u32

var offset : Offset

func Echo([[unrestrict]] Counter value) Counter {
    return value
}

func Start() i32 {
    offset = 42
    return Echo(offset)
}
