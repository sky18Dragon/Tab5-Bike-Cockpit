"""Send commands to a running OpenOCD over its Tcl RPC port (6666), e.g.
   python3 ocd.py "esp32p4.hp.cpu0 mdw 0x500A0000 101"
"""
import socket, sys
cmds = sys.argv[1:]
s = socket.create_connection(("127.0.0.1", 6666), timeout=10)
def cmd(c):
    s.sendall(c.encode() + b"\x1a")
    buf = b""
    while not buf.endswith(b"\x1a"):
        d = s.recv(65536)
        if not d: break
        buf += d
    return buf[:-1].decode(errors="replace")
for c in cmds:
    print(">", c)
    print(cmd(c))
