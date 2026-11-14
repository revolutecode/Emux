[Main]

enum Color : u8 {
  Red = 255,
  Green
}

global Start() u8 {
  return static_cast<u8>(Color::Red)
}
