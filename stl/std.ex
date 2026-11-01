# Ponto de entrada da STL padrao de Emux.
# Cada modulo tem o seu proprio guard; incluir "std.ex" e depois "io.ex"
# (ou o contrario) nao duplica declaracoes.

@ifndef EMUX_STL_STD
@define EMUX_STL_STD

@include "windows.ex"
@include "io.ex"
@include "file.ex"

@endif
