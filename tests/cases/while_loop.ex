[Main]

global i : i32
global sum : i32

global Start() i32 {
    i = 1
    sum = 0
    while (i <= 5) {
        sum = sum + i
        i = i + 1
    }
    return sum
}
