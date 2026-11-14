[Main]

[[import("kernel32")]]
global Native(f32 v) i32

global Start() i32 {
    return Native(1)
}
