// STD - Gestao de Memoria para Emux
// Fornece alocacao dinamica no heap do processo e manipulacao de blocos de memoria.

#ifndef EMUX_STD_MEM
#define EMUX_STD_MEM

#include "windows.ex"

[std]

// Aloca bloco no heap do processo inicializado com zeros (HEAP_ZERO_MEMORY = 0x08)
global alloc(usize bytes) u8* {
    return windows::HeapAlloc(windows::GetProcessHeap(), 8, bytes)
}

// Libera memoria alocada no heap
global free(u8* ptr) bool {
    if (ptr == null) {
        return 1
    }
    return windows::HeapFree(windows::GetProcessHeap(), 0, ptr) != 0
}

// Copia 'count' bytes da origem para o destino.
// O caminho rapido usa palavras de 8 bytes (x86/x64 toleram acessos nao
// alinhados); o resto e copiado byte a byte.
global copy(u8* dst, u8* src, usize count) void {
    local i : usize = 0
    local words : usize = count / 8

    if (words > 0) {
        local d : u64* = reinterpret_cast<u64*>(dst)
        local s : u64* = reinterpret_cast<u64*>(src)
        local w : usize = 0
        while (w < words) {
            d[w] = s[w]
            w = w + 1
        }
        i = words * 8
    }

    while (i < count) {
        dst[i] = src[i]
        i = i + 1
    }
}

global mem_copy(u8* dst, u8* src, usize count) void {
    copy(dst, src, count)
}

// Preenche 'count' bytes do destino com o valor 'val'.
// O byte e replicado por toda a palavra de 8 bytes para o caminho rapido.
global set(u8* dst, u8 val, usize count) void {
    local i : usize = 0
    local words : usize = count / 8

    if (words > 0) {
        local pattern : u64 =
            static_cast<u64>(val) * static_cast<u64>(0x0101010101010101)
        local d : u64* = reinterpret_cast<u64*>(dst)
        local w : usize = 0
        while (w < words) {
            d[w] = pattern
            w = w + 1
        }
        i = words * 8
    }

    while (i < count) {
        dst[i] = val
        i = i + 1
    }
}

global mem_set(u8* dst, u8 val, usize count) void {
    set(dst, val, count)
}

// Compara dois blocos de memoria; retorna 1 se forem identicos, 0 caso contrario
global equal(u8* a, u8* b, usize count) bool {
    local words : usize = count / 8

    if (words > 0) {
        local wa : u64* = reinterpret_cast<u64*>(a)
        local wb : u64* = reinterpret_cast<u64*>(b)
        local w : usize = 0
        while (w < words) {
            if (wa[w] != wb[w]) {
                return 0
            }
            w = w + 1
        }
    }

    local i : usize = words * 8
    while (i < count) {
        if (a[i] != b[i]) {
            return 0
        }
        i = i + 1
    }
    return 1
}

global mem_equal(u8* a, u8* b, usize count) bool {
    return equal(a, b, count)
}

#endif
