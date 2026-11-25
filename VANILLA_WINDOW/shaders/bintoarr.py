import sys
import struct

def bin_to_c_array(input_file, array_name="bin"):
    with open(input_file, "rb") as f:
        data = f.read()

    padding = (4 - (len(data) % 4)) % 4
    data += b'\x00' * padding

    values = struct.unpack("<{}I".format(len(data)//4), data)

    print(f"const uint32_t {array_name}[] = {"{"}")

    for i, val in enumerate(values):
        if i % 8 == 0:
            print("    ", end="")

        print(f"0x{val:08x}, ", end="")

        if i % 8 == 7:
            print()

    if len(values) % 8 != 0:
        print()

    print("};")
    print(f"const size_t {array_name}_len = {len(values)};")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(1)

    bin_to_c_array(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "bin")
