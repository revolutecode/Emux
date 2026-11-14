[Main]

global i : i32
global sum : i32

global Start() i32 {
    i = 1
    sum = 0
    while (i <= 100) {
        if (i > 4) {
            break
        }
        sum = sum + i
        i = i + 1
    }
    return sum
}
