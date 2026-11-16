[Main]

[[packed]] struct Pair { a : u16, b : u16 }

// [[restrict]] exige identidade nominal: um literal agregado anonimo nao tem
// nome de tipo, pelo que e rejeitado como argumento.
global Sum([[restrict]] Pair p) i32 {
    return p.a + p.b
}

global Start() i32 {
    return Sum({ 10, 20 })
}