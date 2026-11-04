[Main]

enum {
  Ok,
  Warn = 5,
  Fail
}

enum Msg : B16 {
  Hello = "hi",
  Bye = "adeus"
}

func Start() u8 {
  return Fail - Warn + *Msg::Hello - 103
}
