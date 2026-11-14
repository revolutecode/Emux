[Main]

using Counter = u32
using Offset = u32

global offset : Offset

global Echo([[unrestrict]] Counter value) Counter {
    return value
}

global Start() i32 {
    offset = 42
    return Echo(offset)
}
