[Main]

@define N 6
@define MUL(a, b) (a * b)

global Start() i32 {
    return MUL(N, 7)
}