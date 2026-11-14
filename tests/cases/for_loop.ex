[Main]

global i : i32
global sum : i32

global Start() i32 {
    sum = 0
    for (i = 0; i < 10; i = i + 1) {
        if (i % 2 == 0) {
            sum = sum + i
        }
    }
    return sum
}
