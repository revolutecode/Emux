[Main]

enum Color : u8 {
  Red,
  Red
}

global Start() u8 {
  return static_cast<u8>(Color::Red)
}
