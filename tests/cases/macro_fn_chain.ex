[Main]

#define INC(x) (x + 1)
#define DOUBLE(x) INC(INC(x))

global Start() i32 {
    return DOUBLE(40)
}