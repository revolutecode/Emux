[Main]

[[packed]] struct A { x : u16, y : u16 }
[[packed]] [[unrestrict]] struct B { x : u16, y : u16 }

// Sem [[restrict]], o parametro aceita uma struct estruturalmente equivalente
// ([[unrestrict]] de qualquer dos lados), tal como na copia por bloco.
global Take(A a) i32 {
    return a.x + a.y
}

global Start() i32 {
    local b : B = { 10, 20 }
    return Take(b)
}