[Main]

[[const]] var text : B13 = "Hello, World"

func Start() i32 {
    # text tem 13 bytes.
    # (text + 7) avanca a cabeca 7 bytes -> restam 6 bytes ("World\0"), aponta para 'W' (87).
    # ((text + 7) - 2) encolhe a cauda em 2 bytes -> restam 4 bytes ("Worl"), aponta ainda para 'W'.
    return *((text + 7) - 2)
}
