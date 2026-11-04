[Main]

enum Msg : B16 {
  Hello = 1
}

func Start() u8 {
  return *Msg::Hello
}
