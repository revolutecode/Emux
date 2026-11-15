#include "std.ex"

[Main]

global Start() i32 {
    std::print_int(-9223372036854775807 - 1)
    return 42
}
