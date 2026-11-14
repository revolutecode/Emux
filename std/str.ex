# STD - Manipulacao de Cadeias de Caracteres (Strings) para Emux
# Trabalha com ponteiros de bytes terminados em zero (ASCII / UTF-8 canonico).

@ifndef EMUX_STD_STR
@define EMUX_STD_STR

[std]

# Retorna o comprimento da cadeia ate o byte zero
func len(u8* s) usize {
    if (s == null) {
        return 0
    }
    local count : usize = 0
    while (s[count] != 0) {
        count = count + 1
    }
    return count
}

func str_len(u8* s) usize {
    return len(s)
}

# Compara lexicograficamente duas cadeias (estilo strcmp)
# Retorna < 0 se s1 < s2, 0 se s1 == s2, > 0 se s1 > s2
func str_cmp(u8* s1, u8* s2) i32 {
    if (s1 == null) {
        if (s2 == null) {
            return 0
        }
        return -1
    }
    if (s2 == null) {
        return 1
    }

    local i : usize = 0
    while (s1[i] != 0 && s1[i] == s2[i]) {
        i = i + 1
    }

    local c1 : i32 = static_cast<i32>(s1[i])
    local c2 : i32 = static_cast<i32>(s2[i])
    return c1 - c2
}

# Retorna 1 se ambas as cadeias forem identicas, 0 caso contrario
func str_equal(u8* s1, u8* s2) bool {
    return str_cmp(s1, s2) == 0
}

func streq(u8* s1, u8* s2) bool {
    return str_equal(s1, s2)
}

# Verifica se a cadeia comeca com o prefixo indicado
func starts_with(u8* s, u8* prefix) bool {
    if (s == null || prefix == null) {
        return 0
    }
    local i : usize = 0
    while (prefix[i] != 0) {
        if (s[i] != prefix[i]) {
            return 0
        }
        i = i + 1
    }
    return 1
}

# Verifica se a cadeia termina com o sufixo indicado
func ends_with(u8* s, u8* suffix) bool {
    if (s == null || suffix == null) {
        return 0
    }
    local s_len : usize = len(s)
    local sub_len : usize = len(suffix)

    if (sub_len > s_len) {
        return 0
    }

    local offset : usize = s_len - sub_len
    local i : usize = 0
    while (i < sub_len) {
        if (s[offset + i] != suffix[i]) {
            return 0
        }
        i = i + 1
    }
    return 1
}

# Copia de s para dst ate no maximo max_len - 1 bytes e adiciona terminador zero
func str_copy(u8* dst, u8* src, usize max_len) usize {
    if (dst == null || max_len == 0) {
        return 0
    }
    if (src == null) {
        dst[0] = 0
        return 0
    }

    local i : usize = 0
    local limit : usize = max_len - 1

    while (i < limit && src[i] != 0) {
        dst[i] = src[i]
        i = i + 1
    }
    dst[i] = 0
    return i
}

# Verifica se o caractere e digito decimal '0'-'9'
func is_digit(u8 c) bool {
    return c >= 48 && c <= 57
}

# Converte caractere para maiusculo
func to_upper(u8 c) u8 {
    if (c >= 97 && c <= 122) {
        return c - 32
    }
    return c
}

# Converte caractere para minusculo
func to_lower(u8 c) u8 {
    if (c >= 65 && c <= 90) {
        return c + 32
    }
    return c
}

# Converte string decimal para inteiro i64 (com suporte a sinal negativo)
func atoi(u8* s) i64 {
    if (s == null) {
        return 0
    }

    local i : usize = 0
    local sign : i64 = 1

    if (s[0] == 45) { # '-'
        sign = -1
        i = 1
    } else {
        if (s[0] == 43) { # '+'
            i = 1
        }
    }

    local res : i64 = 0
    while (s[i] != 0 && is_digit(s[i])) {
        local digit : i64 = static_cast<i64>(s[i] - 48)
        res = (res * 10) + digit
        i = i + 1
    }

    return res * sign
}

# Converte inteiro i64 para representacao decimal ASCII em buf
# Retorna a quantidade de caracteres gravados
func itoa(i64 val, u8* buf, usize buf_len) usize {
    if (buf == null || buf_len < 2) {
        return 0
    }

    if (val == 0) {
        buf[0] = 48 # '0'
        buf[1] = 0
        return 1
    }

    local is_neg : u8 = 0
    local n : i64 = val
    if (val < 0) {
        is_neg = 1
        n = -val
    }

    # Gera digitos invertidos
    local temp : B32
    local count : usize = 0

    while (n > 0 && count < 30) {
        local rem : i64 = n % 10
        temp[count] = 48 + static_cast<u8>(rem)
        n = n / 10
        count = count + 1
    }

    if (is_neg != 0) {
        temp[count] = 45 # '-'
        count = count + 1
    }

    # Inverte para o buffer de destino
    local written : usize = 0
    while (written < count && written < (buf_len - 1)) {
        buf[written] = temp[count - 1 - written]
        written = written + 1
    }
    buf[written] = 0

    return written
}

@endif
