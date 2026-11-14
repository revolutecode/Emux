[Main]

enum Msg : B16 {
  Hello = 1
}

global Start() u8 {
  return *Msg::Hello
}
