#!/usr/bin/env python3
"""Fail the build if any EasyFlash bank switch is emitted inside a bank.

far_call_bank() (src/memmap.h) writes $DE00 inline. Executed from code that
itself lives in the $8000-$BFFF window, the write swaps the window under the
next instruction fetch and execution runs off into the new bank's bytes.
Only resident code may switch banks; banked code must call a resident
__noinline trampoline instead (see src/disk_io.c).

Reads the Oscar64 listing (build/game.asm). Resident lines look like
"0a7c : 8d 00 de STA $de00"; banked ones carry a bank prefix,
"06:8039 : 8d 00 de STA $de00".
"""
import re
import sys

BANKED_WRITE = re.compile(r"^([0-9a-fA-F]{2}):([0-9a-fA-F]{4}) : 8d 00 de\b")
LABEL = re.compile(r"^([A-Za-z_][\w@.]*):")


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "build/game.asm"
    label = "?"
    bad = []
    with open(path, encoding="utf-8", errors="replace") as listing:
        for line in listing:
            match = LABEL.match(line)
            if match:
                label = match[1]
                continue
            match = BANKED_WRITE.match(line)
            if match:
                bad.append((match[1], match[2], label))
    if bad:
        print(f"error: {len(bad)} bank switch(es) emitted inside a bank "
              f"({path}); banked code must use a resident trampoline:",
              file=sys.stderr)
        for bank, address, name in bad:
            print(f"  bank {bank} ${address}  in {name}", file=sys.stderr)
        return 1
    print("bank-switch check: no $DE00 writes inside banked code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
