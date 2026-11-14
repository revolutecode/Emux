[Main]

global i : i32

# 'for (;;)' nao tem teste nem passo: sai apenas pelo 'break'.
global Start() i32 {
    i = 0
    for (;;) {
        i = i + 1
        if (i == 5) {
            break
        }
    }
    return i
}
