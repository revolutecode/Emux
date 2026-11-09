@include "io.ex"

[Main]

[[const]] var text : B13 = "Hello, World"

func Start() u8 {
  io::println(text)

  return 0
}
