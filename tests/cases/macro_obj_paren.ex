[Main]

// Espaco antes de '(': macro OBJETO cujo corpo comeca por '(' (semantica C).
// Nao e uma macro com parametros; o corpo e substituido textualmente.
#define X (20 + 22)

global Start() i32 {
    return X
}