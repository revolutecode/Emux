@include "emux/sdk/windows.ex"

[Main]

func Start() i32 {
    windows::Sleep(0)
    windows::GetLastError()
    return 42
}
