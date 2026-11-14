[Main]

global i : i32
global sum : i32

global Start() i32 {
    i = 0
    sum = 0
    while (i < 8) {
        i = i + 1
        if (i % 2 == 0) {
            continue
        }
        sum = sum + i
    }
    if (i != 8) {
        sum = sum + 100
    }
    return sum
}
