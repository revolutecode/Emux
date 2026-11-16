// Um atributo de funcao aplicado a seccao propaga-se as funcoes do escopo.
// [[noframe] Main] exige que as funcoes nao criem locais; Start nao cria.
[[noframe] Main]

global Start() i32 {
    return 3
}