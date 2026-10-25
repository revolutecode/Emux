[Main2]

var a : i32
var b : i32
var c : i32
var d : i32
var e : i32
var f : i32
var g : i32
var i : u32

func Start() u8 {
    a = 100
    b = 20
    i = -10

    # Divisão binária
    c = a / b       # 100 / 20 = 5

    # Divisão com imediato
    d = c / 2       # 5 / 2 = 2

    # Unary Neg (-)
    e = -d          # -2

    # Unary Not (~)
    f = ~a          # ~100

    g = i + a

    return 0
}
