import socket, time, sys
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
s.settimeout(0.3)
dst = sys.argv[1] if len(sys.argv) > 1 else '255.255.255.255'
s.sendto(b'{"type":"discover"}', (dst, 7088))
end = time.time() + 1.0
while time.time() < end:
    try:
        data, addr = s.recvfrom(2000)
        print(addr[0], data.decode())
    except socket.timeout:
        pass
