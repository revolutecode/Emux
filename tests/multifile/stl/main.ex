@include "windows.ex"

[Main]

global Start() i32 {
    windows::Sleep(0)
    windows::GetLastError()
    return 42
}
