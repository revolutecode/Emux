[Main]

enum Color : u8 {
  Red = 255,
  Green
}

func Start() u8 {
  return static_cast<u8>(Color::Red)
}
