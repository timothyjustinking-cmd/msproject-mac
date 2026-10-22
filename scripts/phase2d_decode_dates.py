#!/usr/bin/env python3
"""Decode mystery flat buffer values using the known packed date format.

Format: (day_1indexed << 16) | (minute_of_day * 10)
Day 1 = 1984-01-01, sentinel = 0xFFFFFFFF
"""

from datetime import datetime, timedelta

def decode_date(packed):
    if packed == 0xFFFFFFFF:
        return "NA (sentinel)"
    day = packed >> 16
    min_x10 = packed & 0xFFFF
    minute = min_x10 // 10
    if day < 1 or day > 100000:
        return f"INVALID day={day}"
    dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
    hours = minute // 60
    mins = minute % 60
    return f"{dt.strftime('%Y-%m-%d')} {hours:02d}:{mins:02d} (day={day}, min={minute})"

def encode_date(dt_str, time_str="08:00"):
    """Encode a date string to packed format for verification."""
    dt = datetime.strptime(dt_str, "%Y-%m-%d")
    h, m = map(int, time_str.split(":"))
    day = (dt - datetime(1984, 1, 1)).days + 1
    minute = h * 60 + m
    packed = (day << 16) | (minute * 10)
    return packed

# Mystery values from the flat buffer
values = {
    "prio-116": 0x3BF912C0,
    "prio-108": 0x3BF912C0,
    "prio-100": 0x000E5B00,
    "prio -96": 0x000E5B00,
    "prio -92": 0x000E5B00,
}

print("Decoding mystery flat buffer values as packed dates:")
print("=" * 70)
for label, val in values.items():
    result = decode_date(val)
    print(f"  {label}: 0x{val:08X} -> {result}")

# Also decode 0x41424F80 (appeared after assignment)
print(f"\n  prio -80: 0x41424F80 -> {decode_date(0x41424F80)}")

# Verify with known COM dates from the current project state
print("\nVerification with known dates:")
known = {
    "Start (2026-01-12 08:00)": encode_date("2026-01-12", "08:00"),
    "Finish (2026-01-16 17:00)": encode_date("2026-01-16", "17:00"),
    "Created (2026-10-20 18:45)": encode_date("2026-10-20", "18:45"),
    "LateStart (2026-10-13 08:00)": encode_date("2026-10-13", "08:00"),
    "LateFinish (2026-10-19 17:00)": encode_date("2026-10-19", "17:00"),
}
for label, packed in known.items():
    print(f"  {label}: 0x{packed:08X} -> {decode_date(packed)}")

# Now search for these encoded dates in the flat buffer
# We need to re-read the buffer to do this
print("\nSearching flat buffer for known date encodings:")
import ctypes
import struct
import sys
import subprocess

sys.stdout.reconfigure(encoding='utf-8', errors='replace')
k32 = ctypes.WinDLL('kernel32', use_last_error=True)

r = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq WINPROJ.EXE', '/FO', 'CSV', '/NH'],
                   capture_output=True, text=True)
pid = int([l for l in r.stdout.strip().split('\n') if 'WINPROJ' in l][0].split(',')[1].strip('"'))
handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)

prio_addr = 0x23b7ef6dd3c

buf_c = ctypes.create_string_buffer(512)
n = ctypes.c_size_t()
k32.ReadProcessMemory(handle, ctypes.c_void_p(prio_addr - 128), buf_c, 512, ctypes.byref(n))
buf = buf_c.raw[:n.value]

for label, packed in known.items():
    packed_bytes = struct.pack('<I', packed)
    for off in range(0, len(buf) - 3, 4):
        u32 = struct.unpack_from('<I', buf, off)[0]
        if u32 == packed:
            rel = off - 128
            print(f"  FOUND {label} at prio{rel:+d}")

# Also decode ALL u32s in the buffer as dates to find any valid ones
print("\nAll valid packed dates in flat buffer (prio-128 to prio+384):")
for off in range(0, len(buf) - 3, 4):
    u32 = struct.unpack_from('<I', buf, off)[0]
    if u32 == 0 or u32 == 0xFFFFFFFF:
        continue
    day = u32 >> 16
    min_x10 = u32 & 0xFFFF
    if 1 <= day <= 50000 and 0 <= min_x10 <= 14400:  # max 24h*60min*10
        minute = min_x10 // 10
        if minute < 1440:  # valid minute of day
            dt = datetime(1984, 1, 1) + timedelta(days=day - 1)
            if 2000 <= dt.year <= 2100:  # reasonable date range
                hours = minute // 60
                mins = minute % 60
                rel = off - 128
                print(f"  prio{rel:+4d}: 0x{u32:08X} -> {dt.strftime('%Y-%m-%d')} {hours:02d}:{mins:02d}")

k32.CloseHandle(handle)
print("\nDone.")
