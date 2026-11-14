[Main]

enum Msg : B4 {
  Hello = "troppo longo"
}

global Start() u8 {
  return *Msg::Hello
}
