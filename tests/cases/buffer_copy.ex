[Main]

global source : B16
global destination : B16

global Start() i32 {
    source = "hello"
    destination = source
    return *destination
}
