[Main]

local my_var : i32 = 32

local Helper() i32 {
    return my_var
}

global Start() i32 {
    return GetA() + Helper()
}
