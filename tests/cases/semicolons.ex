[Main];

global value : u32;

global Start() i32 {
    value = 40;
    value = value + 2;
    return value;
};
