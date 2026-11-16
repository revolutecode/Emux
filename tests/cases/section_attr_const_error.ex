// [[const] Main] propaga imutabilidade as globais: atribuir a x e erro.
[[const] Main]

global x : i32 = 5

global Start() i32 {
    x = 9
    return x
}