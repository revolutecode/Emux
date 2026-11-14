[Main]

global counter : u32 = 7
global big : i64 = -3

global Start() i32 {
    counter += 1
    return counter + big
}
