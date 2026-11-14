[Main]

global i : i32
global j : i32
global total : i32

# O 'break' interior so encerra o loop mais interior: o exterior continua.
global Start() i32 {
    total = 0
    for (i = 0; i < 3; i = i + 1) {
        for (j = 0; j < 5; j = j + 1) {
            if (j == 2) {
                break
            }
            total = total + 1
        }
        if (j != 2) {
            total = total + 1000
        }
        total = total + 10
    }
    return total
}
