@include "std.ex"

[Main]

func Start() i32 {
    local dir : B64
    local p1 : u8*
    p1 = "C:/Emux/Projects/app.ex"
    path::dirname(p1, &dir, 64)

    local expected_dir : u8*
    expected_dir = "C:/Emux/Projects"

    local ok : bool = str::equal(&dir, expected_dir)
    if (ok == 1) {
        return 42
    }

    return 0
}
