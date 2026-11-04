[Main]

enum Msg : B4 {
  Hello = "troppo longo"
}

func Start() u8 {
  return *Msg::Hello
}
