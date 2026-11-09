# STL - Manipulacao de Caminhos de Ficheiros (Paths) para Emux
# Suporta separadores Windows (\) e Unix (/).

@ifndef EMUX_STL_PATH
@define EMUX_STL_PATH

@include "str.ex"

[path]

# Retorna 1 se o caractere for barra direta ou invertida, 0 caso contrario
func is_sep(u8 c) bool {
    return c == 47 || c == 92 # '/' ou '\\'
}

# Retorna ponteiro para o nome do ficheiro (apos a ultima barra)
func basename(u8* p) u8* {
    if (p == null) {
        return null
    }

    local last_slash : u8*
    last_slash = p
    local i : usize = 0

    while (p[i] != 0) {
        if (is_sep(p[i])) {
            last_slash = p + i + 1
        }
        i = i + 1
    }

    return last_slash
}

# Extrai o diretorio pai para out_buf (tudo antes da ultima barra)
func dirname(u8* p, u8* out_buf, usize buf_len) usize {
    if (p == null || out_buf == null || buf_len < 2) {
        return 0
    }

    local p_len : usize = str::len(p)
    if (p_len == 0) {
        out_buf[0] = 46 # '.'
        out_buf[1] = 0
        return 1
    }

    local last_idx : i64 = -1
    local i : usize = 0

    while (i < p_len) {
        if (is_sep(p[i])) {
            last_idx = static_cast<i64>(i)
        }
        i = i + 1
    }

    if (last_idx <= 0) {
        # Sem barra ou barra no inicio
        if (last_idx == 0) {
            out_buf[0] = p[0]
            out_buf[1] = 0
            return 1
        }
        out_buf[0] = 46 # '.'
        out_buf[1] = 0
        return 1
    }

    local copy_len : usize = static_cast<usize>(last_idx)
    if (copy_len >= buf_len) {
        copy_len = buf_len - 1
    }

    local j : usize = 0
    while (j < copy_len) {
        out_buf[j] = p[j]
        j = j + 1
    }
    out_buf[j] = 0
    return j
}

# Retorna ponteiro para o '.' da extensao do ficheiro, ou ponteiro para o fim da string se nao houver
func extension(u8* p) u8* {
    if (p == null) {
        return null
    }

    local base : u8*
    base = basename(p)
    local dot : u8*
    dot = null
    local i : usize = 0

    while (base[i] != 0) {
        if (base[i] == 46) { # '.'
            dot = base + i
        }
        i = i + 1
    }

    if (dot == null) {
        return base + i
    }

    return dot
}

# Verifica se o ficheiro tem a extensao fornecida (ex: ".ex")
func has_extension(u8* p, u8* ext) bool {
    if (p == null || ext == null) {
        return 0
    }
    local ext_ptr : u8*
    ext_ptr = extension(p)
    return str::equal(ext_ptr, ext)
}

# Concatena dois caminhos garantindo uma unica barra de separacao
func join(u8* out_buf, usize buf_len, u8* p1, u8* p2) usize {
    if (out_buf == null || buf_len < 2) {
        return 0
    }

    local len1 : usize = str::len(p1)
    local len2 : usize = str::len(p2)

    local written : usize = 0

    # Copia p1
    while (written < len1 && written < (buf_len - 1)) {
        out_buf[written] = p1[written]
        written = written + 1
    }

    # Adiciona separador se necessario
    if (written > 0 && !is_sep(out_buf[written - 1]) && written < (buf_len - 1)) {
        out_buf[written] = 92 # '\\'
        written = written + 1
    }

    # Pula separadores no inicio de p2
    local p2_start : usize = 0
    while (p2_start < len2 && is_sep(p2[p2_start])) {
        p2_start = p2_start + 1
    }

    # Copia p2
    while (p2_start < len2 && written < (buf_len - 1)) {
        out_buf[written] = p2[p2_start]
        written = written + 1
        p2_start = p2_start + 1
    }

    out_buf[written] = 0
    return written
}

@endif
