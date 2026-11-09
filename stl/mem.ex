# STL - Gestao de Memoria para Emux
# Fornece alocacao dinamica no heap do processo e manipulacao de blocos de memoria.

@ifndef EMUX_STL_MEM
@define EMUX_STL_MEM

@include "windows.ex"

[mem]

# Aloca bloco no heap do processo inicializado com zeros (HEAP_ZERO_MEMORY = 0x08)
func alloc(usize bytes) u8* {
    return windows::HeapAlloc(windows::GetProcessHeap(), 8, bytes)
}

# Libera memoria alocada no heap
func free(u8* ptr) bool {
    if (ptr == null) {
        return 1
    }
    return windows::HeapFree(windows::GetProcessHeap(), 0, ptr) != 0
}

# Copia 'count' bytes da origem para o destino
func copy(u8* dst, u8* src, usize count) void {
    local i : usize = 0
    while (i < count) {
        dst[i] = src[i]
        i = i + 1
    }
}

# Preenche 'count' bytes do destino com o valor 'val'
func set(u8* dst, u8 val, usize count) void {
    local i : usize = 0
    while (i < count) {
        dst[i] = val
        i = i + 1
    }
}

# Compara dois blocos de memoria; retorna 1 se forem identicos, 0 caso contrario
func equal(u8* a, u8* b, usize count) bool {
    local i : usize = 0
    while (i < count) {
        if (a[i] != b[i]) {
            return 0
        }
        i = i + 1
    }
    return 1
}

@endif
