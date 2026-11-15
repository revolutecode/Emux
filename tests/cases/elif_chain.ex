[Main]

// O 'elif' tem de decidir sobre o contexto que o contem, nao sobre o IsActive
// atual: depois de um '#if' falso, o ramo alternativo ainda pode ser escolhido.
#if 0
global Start() i32 {
    return 1
}
#elif 0
global Start() i32 {
    return 2
}
#elif 1
global Start() i32 {
    return 4
}
#else
global Start() i32 {
    return 3
}
#endif
