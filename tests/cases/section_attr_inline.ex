// Propriedades: a seccao [nome] aceita atributos inline. [[const] Main]
// aplica [[const]] a todas as globais do escopo (aqui, x fica imutavel).
[[const] Main]

global x : i32 = 5

global Start() i32 {
    return x
}