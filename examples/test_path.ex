@include "std.ex"

[Main]

func Start() i32 {
    local caminho : u8*
    caminho = "C:/Emux/Projects/app.ex"

    local base : u8*
    base = path::basename(caminho)

    local ext : u8*
    ext = path::extension(caminho)

    local expected_base : u8*
    expected_base = "app.ex"

    local expected_ext : u8*
    expected_ext = ".ex"

    local ok1 : bool = str::equal(base, expected_base)
    local ok2 : bool = str::equal(ext, expected_ext)

    if (ok1 == 1 && ok2 == 1) {
        return 42
    }

    return 0
}
