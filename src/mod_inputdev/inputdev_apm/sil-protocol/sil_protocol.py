"""Python bindings for sil_protocol.h, parsed from the C header at import time."""

import ctypes
import re
import sys
from pathlib import Path

# ── C-to-ctypes type map ────────────────────────────────────────────────────
_CTYPE_MAP = {
    "uint8_t": ctypes.c_uint8,
    "uint16_t": ctypes.c_uint16,
    "uint32_t": ctypes.c_uint32,
    "int16_t": ctypes.c_int16,
    "int32_t": ctypes.c_int32,
    "float": ctypes.c_float,
    "double": ctypes.c_double,
}


def _parse_header(header_path: Path) -> tuple[dict[str, int], dict[str, list]]:
    text = header_path.read_text()

    # Parse enum values: SIM_MSG_FOO = N
    enums: dict[str, int] = {}
    for m in re.finditer(r"(SIM_MSG_\w+)\s*=\s*(\d+)", text):
        enums[m.group(1)] = int(m.group(2))

    # Parse structs
    structs: dict[str, list] = {}
    # Match: struct Name { ... };
    for m in re.finditer(r"struct\s+(\w+)\s*\{([^}]+)\}", text):
        name = m.group(1)
        body = m.group(2)
        fields = []
        for line in body.split(";"):
            line = line.strip()
            if not line or line.startswith("//"):
                continue
            # Remove inline comments
            line = re.sub(r"//.*", "", line).strip()
            if not line:
                continue
            # "TypeName field1, field2, field3"
            parts = line.split()
            type_name = parts[0]
            field_names = " ".join(parts[1:])
            for fname in field_names.split(","):
                fname = fname.strip()
                if fname:
                    fields.append((type_name, fname))
        structs[name] = fields

    return enums, structs


def _build_module(enums: dict[str, int], structs: dict[str, list]) -> dict[str, type]:
    ns: dict[str, type] = {}

    # Register enum constants
    for k, v in enums.items():
        ns[k] = v

    # Build ctypes structs (order matters — SimMsgHeader must exist before others)
    built: dict[str, type] = {}
    for struct_name, fields in structs.items():
        cfields = []
        for type_name, field_name in fields:
            if type_name in _CTYPE_MAP:
                cfields.append((field_name, _CTYPE_MAP[type_name]))
            elif type_name in built:
                cfields.append((field_name, built[type_name]))
            else:
                raise ValueError(f"Unknown type {type_name!r} in {struct_name}.{field_name}")

        cls = type(
            struct_name,
            (ctypes.LittleEndianStructure,),
            {
                "_pack_": 1,
                "_fields_": cfields,
            },
        )
        built[struct_name] = cls
        ns[struct_name] = cls

    return ns


# ── Parse and build on import ───────────────────────────────────────────────
_header_path = Path(__file__).parent / "sil_protocol.h"
_enums, _structs = _parse_header(_header_path)
_ns = _build_module(_enums, _structs)

# Inject into module namespace so "from sil_protocol import SimActuatorMsg" works
_self = sys.modules[__name__]
for _k, _v in _ns.items():
    setattr(_self, _k, _v)

# ── Verify sizes match static_asserts in the header ─────────────────────────
_EXPECTED_SIZES = {}
_header_text = _header_path.read_text()
for _m in re.finditer(r"static_assert\(sizeof\((\w+)\)\s*==\s*(\d+)", _header_text):
    _EXPECTED_SIZES[_m.group(1)] = int(_m.group(2))

for _name, _expected in _EXPECTED_SIZES.items():
    _cls = _ns.get(_name)
    if _cls is not None:
        _actual = ctypes.sizeof(_cls)
        assert _actual == _expected, f"{_name}: sizeof={_actual}, expected={_expected}"


if __name__ == "__main__":
    for name, expected in _EXPECTED_SIZES.items():
        print(f"{name}: {expected} bytes ✓")
    for name, val in _enums.items():
        print(f"{name} = {val}")
