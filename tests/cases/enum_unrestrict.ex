[Main]

[[unrestrict]]
enum Level : u8 {
  Low = 7,
  High
}

global Use(u8 value) u8 {
  return value
}

global Start() u8 {
  return Use(Level::High)
}
