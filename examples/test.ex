@include "io.ex"

[Main]

[[const]] var text : B13 = "Hello, World"

[[import("user32")]]
func MessageBoxA(HANDLE hwnd, LPCVOID lpText, LPCVOID lpCaption, u32 uType) i32

func Start() u8 {
  io::println(text);
  # io::read(input, 16);
  # io::println(input);

  MessageBoxA(0, text, text, 0)
  return 0
}
