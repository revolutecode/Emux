[Main]

[[import("kernel32")]]
func Native(f32 v) i32

func Start() i32 {
    return Native(1)
}
