from struct import Struct
from collections import namedtuple
import re


class CStruct:
    def __init__(self, cformat):
        items = re.findall("struct (.*?){(.*?)}", cformat)
        struct_name = items[0][0]
        struct_values = re.findall("([^\s]+?) (.*?);",items[0][1]+" ")
        datatypes = {"uint8_t": "c",
                     "uint16_t": "h",
                     "char": "c",
                     "signed char": "b",
                     "unsigned char": "B",
                     "bool": "?",
                     "short":"h",
                     "unsigned short":"H",
                     "int":"i",
                     "unsigned int":"I",
                     "long":"l",
                     "usigned long": "L",
                     "long long":"q",
                     "unsigned long long":"Q",
                     "ssize_t":"n",
                     "size_t":"N",
                     "float":"f",
                     "double":"d",
                     "char[]":"s"}

        types = [a[0] for a in struct_values]
        var_names = [a[1] for a in struct_values]
        for index, var in enumerate(var_names):
            if "[" in var:
                if types[index] == "char":
                    types[index] = "char[]"
                tup = re.findall("(.*?)\[(\d*)\]", var)[0]
                var_names[index] = tup[0]
                types[index] = (tup[1], types[index])

        format = ""
        for t, n in zip(types, var_names):
            if isinstance(t, tuple):
                type = t[0]+ datatypes[t[1]]
            else:
                type = datatypes[t]
            format = format + type


        self.p_struct = Struct(format)
        self.struct_tup = namedtuple(struct_name, " ".join(var_names))

    def pack(self, values):
        return self.p_struct.pack(*values)

    def unpack(self, bytes_string):
        return self.struct_tup(*self.p_struct.unpack(bytes_string))


if __name__ == "__main__":
    Philipp = CStruct("struct Point{short a; short b; long c;};")
    bytes = Philipp.pack([1,2,3])
    print(Philipp.unpack(bytes))
