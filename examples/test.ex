#include "io.ex"

[Main]

//[[const]] global text : B13 = "Hello, World"

global Start() u8 {

  // std::println(text)
  local i : usize
  local i2 : usize
  for (i = 0; i < 100; i++)
  {
    i2++
  }
  
  std::println_int(i)
  std::println_int(i2)
  
  return 0
}