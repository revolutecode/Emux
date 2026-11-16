[Main]

global Start() i32 {
    local v : Types::Shared = { 10, 20 }
    return v.x + v.y + Types::UseSecret()
}