[Types]

using Counter = u32

global Bump(Counter value) Counter {
    return value + 2
}
