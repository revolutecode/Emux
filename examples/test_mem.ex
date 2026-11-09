@include "std.ex"

[Main]

func Start() i32 {
    # 1. Aloca 16 bytes no heap
    local p1 : u8*
    p1 = mem::alloc(16)

    local p2 : u8*
    p2 = mem::alloc(16)

    if (p1 == null || p2 == null) {
        return 0
    }

    # 2. Preenche p1 com 42
    mem::set(p1, 42, 16)

    # 3. Copia p1 para p2
    mem::copy(p2, p1, 16)

    # 4. Verifica se sao iguais
    local iguais : bool = mem::equal(p1, p2, 16)

    # 5. Libera ambos
    mem::free(p1)
    mem::free(p2)

    if (iguais == 1) {
        return 42
    }

    return 1
}
