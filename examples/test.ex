@include "io.ex"

[Main]

[[const]] global text : B13 = "Hello, World"

global Start() u8 {
  std::println(text)

  return 0
}
