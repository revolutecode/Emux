[Main]

[[packed]] struct Inner { x : u16, y : u16 }
[[packed]] struct Outer { inner : Inner, z : u8 }

// Um membro struct tambem se converte para buffer: expoe so os bytes do
// membro (x=10, y=20), nao os campos seguintes de Outer.
global Start() i32 {
    local o : Outer = { { 10, 20 }, 7 }
    local buf : B8 = o.inner
    return buf[0] + buf[2]*2 + buf[4]*3
}