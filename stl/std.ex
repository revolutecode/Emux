# Ponto de entrada da STL padrao de Emux.
# Cada modulo tem o seu proprio guard; incluir "std.ex" e depois "io.ex"
# (ou o contrario) nao duplica declaracoes.

@ifndef EMUX_STL_STD
@define EMUX_STL_STD

@define true 1
@define false 0
@define null reinterpret_cast<u8*>(static_cast<usize>(0))

@include "windows.ex"
@include "io.ex"
@include "file.ex"
@include "mem.ex"
@include "str.ex"
@include "math.ex"
@include "path.ex"

@endif
