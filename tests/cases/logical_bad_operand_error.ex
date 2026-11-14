[Main]

[[const]] global text : B13 = "Hello, World"

global Start() i32 {
    # Um bloco nao e um condicional: o tamanho e estatico e o endereco nunca
    # e zero, por isso '&&' entre um bloco e um inteiro nao tem significado.
    if (text && 1) {
        return 1
    }

    return 0
}
