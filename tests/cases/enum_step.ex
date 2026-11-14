[Main]

[[step(2)]]
enum Even : u8 {
  Two,
  Four,
  Six
}

[[step(-1)]]
enum Down : i8 {
  High = 3,
  Mid,
  Low
}

[[step(off)]]
enum Flags : u8 {
  None = 0,
  Read = 1,
  Write = 4
}

enum Mixed : u16 {
  A = 10,
  B,
  C = A + 5,
  D = 1 << 3
}

global Start() u8 {
  return static_cast<u8>(Even::Six) +
         static_cast<u8>(Down::Low) +
         static_cast<u8>(Flags::Write) +
         static_cast<u8>(Mixed::D) -
         static_cast<u8>(Mixed::C)
}
