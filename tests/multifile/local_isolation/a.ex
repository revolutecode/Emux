[Main]

local my_var : i32 = 10

local Helper() i32 {
    return my_var
}

global GetA() i32 {
    return Helper()
}
