import re

with open("shell.cpp", "r") as f:
    code = f.read()

# Fix NaglowekBur
old_naglowek = """NaglowekBur naglowek = {
    {'B', 'U', 'R', '\\0'},
    reinterpret_cast<uint64_t>(&_start),
    0x1000, 0x8000, 0x1001000,
    0x9000, 0x20000, 0x1009000
};"""

new_naglowek = """NaglowekBur naglowek = {
    {'B', 'U', 'R', '\\0'},
    reinterpret_cast<uint64_t>(&_start),
    0x1000, 0x20000, 0x1001000,
    0x21000, 0x40000, 0x1021000
};"""

if old_naglowek in code:
    code = code.replace(old_naglowek, new_naglowek)
else:
    print("Could not find old_naglowek")

# Fix constexprs if they exist
code = code.replace("constexpr uint64_t BUR_TEXT_SIZE   = 0x8000ULL;", "constexpr uint64_t BUR_TEXT_SIZE   = 0x20000ULL;")
code = code.replace("constexpr uint64_t BUR_DATA_OFFSET = 0x9000ULL;", "constexpr uint64_t BUR_DATA_OFFSET = 0x21000ULL;")
code = code.replace("constexpr uint64_t BUR_DATA_VADDR  = 0x1009000ULL;", "constexpr uint64_t BUR_DATA_VADDR  = 0x1021000ULL;")
code = code.replace("constexpr uint64_t BUR_DATA_SIZE   = 0x20000ULL;", "constexpr uint64_t BUR_DATA_SIZE   = 0x40000ULL;")

with open("shell.cpp", "w") as f:
    f.write(code)
