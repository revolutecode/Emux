[Main]

[[step(two)]]
enum Even : u8 {
  Two,
  Four
}

global Start() u8 {
  return static_cast<u8>(Even::Two)
}
