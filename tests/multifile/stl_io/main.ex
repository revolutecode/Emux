@include "std.ex"

[Main]

global path : B40
global handle : std::FILE
global buffer : B16
global length : u32

global Start() i32 {
    path = "tests/out/stl_io_file.txt"
    handle = std::create(path)
    std::write_text(handle, "Emux")
    std::close(handle)
    handle = std::open(path)
    length = std::file_size(handle)
    std::file_read(handle, buffer, 16)
    std::close(handle)
    std::println("STL IO OK")
    return *buffer + length - 4
}
