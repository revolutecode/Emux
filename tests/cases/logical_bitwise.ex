[Main]

func Start() i32 {
    local a : i32 = 6
    local b : i32 = 3

    # '&' e '|' continuam bit-a-bit: 6 & 3 = 2, 4 | 1 = 5
    local mask : i32 = a & b
    local join : i32 = 4 | 1

    # misturados com logicos: (mask != 0) && (join > 4) = 1
    local ok : i32 = mask != 0 && join > 4

    # '||' com um bitwise: (mask == 0) || (join == 5) = 1
    local either : i32 = mask == 0 || join == 5

    # 2 + 5 + 100 + 10 (os codigos de saida so observam o byte baixo)
    return mask + join + ok * 100 + either * 10
}
