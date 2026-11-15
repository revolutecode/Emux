[Main]

#define BAD(a, a) (a + a)

global Start() i32 {
    return BAD(1, 2)
}