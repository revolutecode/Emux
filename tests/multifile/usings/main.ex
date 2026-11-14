[Main]

global count : Types::Counter

global Start() i32 {
    count = 40
    return Types::Bump(count)
}
