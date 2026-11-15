[Main]

global a : i64

global Start() i32 {
    // O literal positivo e inferido como u64, mas cabe em i64: nao ha
    // reinterpretacao e o valor pode entrar sem cast.
    a = 5000000000
    return static_cast<i32>(a & 63)
}
