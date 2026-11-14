# Ponto de entrada da STD padrao de Emux.
# Cada modulo tem o seu proprio guard; incluir "std.ex"
# nao duplica declaracoes.

@ifndef EMUX_STD_STD
@define EMUX_STD_STD

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
