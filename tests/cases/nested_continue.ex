[Main]

global i : i32
global j : i32
global total : i32

# O 'continue' do 'for' interior nao e o do 'while' exterior: este ultimo
# corre o corpo inteiro de cada iteracao.
global Start() i32 {
    total = 0
    i = 0
    while (i < 2) {
        for (j = 0; j < 4; j = j + 1) {
            if (j == 1) {
                continue
            }
            total = total + 1
        }
        i = i + 1
    }
    return total
}
