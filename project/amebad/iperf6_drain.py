import socket, time, sys

s = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('::', 5001))
s.listen(1)
s.settimeout(45)
print("iperf6 drain: listening [::]:5001", flush=True)
try:
    c, a = s.accept()
except socket.timeout:
    print("NO CONNECTION in 45s (firewall? routing?)", flush=True)
    sys.exit(1)
print("conn from", a, flush=True)
c.settimeout(30)
total = 0
t0 = None
try:
    while True:
        d = c.recv(65536)
        if not d:
            break
        if t0 is None:
            t0 = time.time()
        total += len(d)
except socket.timeout:
    pass
dt = (time.time() - t0) if t0 else 0
if dt > 0:
    print("DRAIN: %d bytes in %.2fs = %.2f Mbit/s" % (total, dt, total * 8 / dt / 1e6), flush=True)
else:
    print("DRAIN: %d bytes" % total, flush=True)
