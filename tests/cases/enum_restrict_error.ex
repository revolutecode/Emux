[Main]

using Counter = u8

enum Color : u8 {
  Red = 3,
  Green
}

func Use(Counter value) u8 {
  return static_cast<u8>(value)
}

func Start() u8 {
  return Use(Color::Red)
}
