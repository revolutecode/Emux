[Main]

global text : B8 = "AB"
global copy : B8

global Start() i32 {
    copy = text
    text = "CD"
    return *copy
}
