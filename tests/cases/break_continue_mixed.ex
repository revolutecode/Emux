[Main]

global i : i32
global sum : i32

global Start() i32 {
    sum = 0
    for (i = 0; i < 20; i = i + 1) {
        if (i % 3 == 0) {
            continue
        }
        if (i > 7) {
            break
        }
        sum = sum + i
    }
    return sum
}
