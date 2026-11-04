[Main]

enum Color : u8 {
  Red = 300
}

func Start() u8 {
  return static_cast<u8>(Color::Red)
}
