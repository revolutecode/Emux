[STL]

func Exit(u32 code) u8 {
  exir{exit code}
  return 0
}

[Main]

test : u32
test2 : u32

# vars test:u32,test2:u32
exir {
import ExitProcess,stdcall,4
import GetStdHandle,stdcall,4
import WriteFile,stdcall,20
bytes message,"Oi a todos",13,10
}

func Value() u8 {
  return 0
}

func Start() u8 {
  test = (2 >> 1) - 1
  test2 = 1 ^ 1
  
exir {
call GetStdHandle,-11
call WriteFile,rr0,message,message_len,Main_test2,Main_test
}
  
  STL::Exit(0)

  return 0
}
