"""Replay the ST7121 init table over the DSI host registers via OpenOCD (command mode), without reflashing.

Needs OpenOCD running against the Tab5 (board/esp32p4-builtin.cfg, Tcl RPC on port 6666).
Note: on 2026-10-20 this did NOT work: the DSI host stalled (command FIFO never drained) while the panel
was held in reset, see docs/08 section 7. Kept as a record and as a starting point.

Usage: python3 dsi_reinit.py [--parse-only] [--vpg]
"""
import re, socket, sys, time
from pathlib import Path
HDR = Path(__file__).resolve().parents[2] / "firmware/boards/m5stack_tab5/st7121_init_data.h"
B = 0x500A0000
src = open(HDR).read()
body = src[src.index("tab5_st7121_init_cmds"):]
items = []
for m in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(NULL|\(uint8_t\[\]\)\{([^}]*)\})\s*,\s*(\d+)\s*,\s*(\d+)\s*\}", body, re.S):
    cmd = int(m.group(1), 16)
    data = [int(x, 16) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(3) or "")]
    assert len(data) == int(m.group(4)), (hex(cmd), len(data), m.group(4))
    items.append((cmd, data, int(m.group(5))))
print("parsed %d commands" % len(items))
if "--parse-only" in sys.argv:
    for c, d, t in items: print("  0x%02X %2d bytes delay %d" % (c, len(d), t))
    sys.exit(0)

s = socket.create_connection(("127.0.0.1", 6666), timeout=10)
def cmd(c):
    s.sendall(c.encode() + b"\x1a")
    buf = b""
    while not buf.endswith(b"\x1a"):
        d = s.recv(65536)
        if not d: break
        buf += d
    return buf[:-1].decode(errors="replace")
def rd(a):
    m = re.search(r":\s+([0-9a-f]{8})", cmd("esp32p4.hp.cpu0 mdw 0x%08x 1" % a)); return int(m.group(1), 16)
def wr(a, v): cmd("esp32p4.hp.cpu0 mww 0x%08x 0x%08x" % (a, v))
def wait_idle():
    t0 = time.time()
    while time.time() - t0 < 1.0:
        st = rd(B + 0x74)
        if (st & 0x5) == 0x5:   # gen_cmd_empty and gen_pld_w_empty
            return True
    print("WARN fifo not idle, status=%08x" % st); return False

print("before: mode_cfg=%x lpclk=%x vid_mode=%x cmd_pkt=%x" % (rd(B+0x34), rd(B+0x94), rd(B+0x38), rd(B+0x74)))
wr(B + 0x34, 1)              # command mode
time.sleep(0.05)
for cmd_b, data, delay in items:
    wait_idle()
    n = 1 + len(data)
    if n > 2:
        payload = bytes([cmd_b] + data)
        payload += b"\x00" * (-len(payload) % 4)
        for i in range(0, len(payload), 4):
            wr(B + 0x70, int.from_bytes(payload[i:i+4], "little"))
        wr(B + 0x6C, 0x39 | ((n & 0xFF) << 8) | ((n >> 8) << 16))
    elif n == 2:
        wr(B + 0x6C, 0x15 | (cmd_b << 8) | (data[0] << 16))
    else:
        wr(B + 0x6C, 0x05 | (cmd_b << 8))
    if delay:
        time.sleep(delay / 1000.0)
wait_idle()
time.sleep(0.1)
wr(B + 0x34, 0)              # back to video mode
time.sleep(0.2)
print("after:  mode_cfg=%x phy=%x cmd_pkt=%x int_st0=%08x int_st1=%08x" % (rd(B+0x34), rd(B+0xb0), rd(B+0x74), rd(B+0xbc), rd(B+0xc0)))
if "--vpg" in sys.argv:
    wr(B + 0x38, rd(B + 0x38) | (1 << 16)); print("VPG color bars ON")
