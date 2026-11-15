[Main]

[[packed]]
struct Teste {
    x : u24,
    y : u8
}

global g : Teste

global Start() i32 {
    local s : Teste
    s.x = 5
    s.y = 7
    g.x = 10
    g.y = 20
    local t : Teste = s
    return s.x + s.y + g.x + g.y + t.x + t.y
}