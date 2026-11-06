import serial, sys, time
s = serial.Serial('/dev/ttyUSB0', 115200, timeout=0.2)
for cmd in sys.argv[1:]:
    s.write((cmd + '\n').encode()); end = time.time() + (2.5 if cmd != 'brain' else 3)
    out = b''
    while time.time() < end:
        out += s.read(4096)
    print('>>', cmd); print(out.decode('utf8', 'replace').strip())
