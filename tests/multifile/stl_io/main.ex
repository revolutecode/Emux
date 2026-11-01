@include "std.ex"

[Main]

var path : B40
var handle : file::FILE
var buffer : B16
var length : u32

func Start() i32 {
    path = "tests/out/stl_io_file.txt"
    handle = file::create(path)
    file::write_text(handle, "Emux")
    file::close(handle)
    handle = file::open(path)
    length = file::size(handle)
    file::read(handle, buffer, 16)
    file::close(handle)
    io::println("STL IO OK")
    return *buffer + length - 4
}
