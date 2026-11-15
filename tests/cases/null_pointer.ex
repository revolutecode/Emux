[Main]

global p : u8*

global Start() i32 {
    # 0 e a unica constante inteira que converte implicitamente para ponteiro.
    p = 0
    if (reinterpret_cast<uptr>(p) == 0) {
        return 7
    }
    return 1
}
