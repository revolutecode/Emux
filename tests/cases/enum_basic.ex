[Main]

enum Color : u8 {
  Red,
  Green,
  Blue
}

func Value(Color color) u8 {
  return static_cast<u8>(color)
}

func Start() u8 {
  return Value(Color::Blue) * 20 + static_cast<u8>(Color::Green)
}
