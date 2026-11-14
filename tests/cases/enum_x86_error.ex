[Main]

enum Big : u128 {
  One = 1
}

global Start() u8 {
  return static_cast<u8>(Big::One)
}
