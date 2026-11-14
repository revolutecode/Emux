[Main]

global Fatorial(i32 n) i32 {
    local res : i32 = 1
    local i : i32 = 1

    while (i <= n) {
        res = res * i
        i = i + 1
    }

    return res
}

global Start() i32 {
    # 5! = 120
    local fat5 : i32 = Fatorial(5)

    if (fat5 == 120) {
        return 42
    }

    return 1
}
