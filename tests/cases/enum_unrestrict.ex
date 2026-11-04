[Main]

[[unrestrict]]
enum Level : u8 {
  Low = 7,
  High
}

func Use(u8 value) u8 {
  return value
}

func Start() u8 {
  return Use(Level::High)
}
