[Main]

[[step(off)]]
enum Flags : u8 {
  Read = 1,
  Write
}

func Start() u8 {
  return static_cast<u8>(Flags::Read)
}
