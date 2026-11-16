[Main]

[[packed]] struct A { x : u16, y : u16 }
[[packed]] [[unrestrict]] struct B { x : u16, y : u16 }

// [[restrict]] rejeita uma struct apenas com o mesmo layout: exige o tipo
// nominal exato, mesmo que B seja estruturalmente equivalente via [[unrestrict]].
global Take([[restrict]] A a) i32 {
    return a.x + a.y
}

global Start() i32 {
    local b : B = { 10, 20 }
    return Take(b)
}