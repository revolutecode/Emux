[Main]

global Start() i32 {
    local p : Types::Pair = { 10, 20 }
    local q : Types::Pair = Types::Make()
    return Types::Sum(p) + Types::Sum(q)
}