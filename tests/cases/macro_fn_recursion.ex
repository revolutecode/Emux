[Main]

// Auto-referencia: o guarda activeMacros tem de terminar a expansao (sem loop
// infinito). O resultado nao expande F, logo a chamada falha na semantica.
#define F(x) (F(x) + 0)

global Start() i32 {
    return F(1)
}