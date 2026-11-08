[Main]

func Start() i32 {
    local r : i32 = 3
    local acc : i32
    local i : i32 = 0

    acc = r * 10
    while (i < 4) {
        acc += i
        i = i + 1
    }

    if (acc > 36) {
        local inner : i32 = 1
        acc -= inner
    }

    return acc
}
