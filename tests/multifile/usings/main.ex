[Main]

var count : Types::Counter

func Start() i32 {
    count = 40
    return Types::Bump(count)
}
