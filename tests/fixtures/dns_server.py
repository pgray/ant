import socket
import struct
import sys
import threading

A, NS, CNAME, SOA, PTR, MX, TXT, AAAA, SRV, NAPTR, TLSA, ANY, CAA = 1, 2, 5, 6, 12, 15, 16, 28, 33, 35, 52, 255, 257


def name(text):
    out = b''
    for label in text.rstrip('.').split('.'):
        out += bytes([len(label)]) + label.encode()
    return out + b'\x00'


def text_chunks(chunks):
    return b''.join(bytes([len(c)]) + c.encode() for c in chunks)


RECORDS = {
    'fixture.test': [
        (A, 300, socket.inet_aton('192.0.2.1')),
        (A, 300, socket.inet_aton('192.0.2.2')),
        (AAAA, 300, socket.inet_pton(socket.AF_INET6, '2001:db8::1')),
        (MX, 300, struct.pack('!H', 10) + name('mail.fixture.test')),
        (TXT, 300, text_chunks(['v=spf1', '-all'])),
        (TXT, 300, text_chunks(['second'])),
        (NS, 300, name('ns1.fixture.test')),
        (SOA, 300, name('ns1.fixture.test') + name('hostmaster.fixture.test') + struct.pack('!IIIII', 2024010101, 3600, 600, 86400, 300)),
        (CAA, 300, bytes([0, 5]) + b'issue' + b'letsencrypt.org'),
        (NAPTR, 300, struct.pack('!HH', 100, 10) + text_chunks(['S', 'SIP+D2U', '']) + name('_sip._udp.fixture.test')),
        (TLSA, 300, bytes([3, 1, 1]) + bytes.fromhex('deadbeef')),
    ],
    'alias.fixture.test': [(CNAME, 300, name('fixture.test'))],
    '_sip._tcp.fixture.test': [(SRV, 300, struct.pack('!HHH', 0, 5, 5060) + name('sip.fixture.test'))],
    '1.2.0.192.in-addr.arpa': [(PTR, 300, name('host.fixture.test'))],
    'empty.fixture.test': [],
}


def parse_question(packet):
    offset, labels = 12, []
    while packet[offset]:
        length = packet[offset]
        labels.append(packet[offset + 1:offset + 1 + length].decode())
        offset += length + 1
    qtype, _ = struct.unpack('!HH', packet[offset + 1:offset + 5])
    return '.'.join(labels).lower(), qtype, offset + 5


def answer(packet):
    qname, qtype, end = parse_question(packet)
    if qname == 'slow.fixture.test':
        return None
    records = RECORDS.get(qname)
    rcode = 3 if records is None else 0
    matches = [r for r in (records or []) if qtype == ANY or r[0] == qtype]
    header = struct.pack('!HHHHHH', struct.unpack('!H', packet[:2])[0], 0x8180 | rcode, 1, len(matches), 0, 0)
    body = packet[12:end]
    for rtype, ttl, rdata in matches:
        body += b'\xc0\x0c' + struct.pack('!HHIH', rtype, 1, ttl, len(rdata)) + rdata
    return header + body


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', 0))
    print(sock.getsockname()[1], flush=True)
    threading.Thread(target=lambda: (sys.stdin.read(), sock.close()), daemon=True).start()
    while True:
        try:
            packet, addr = sock.recvfrom(4096)
        except OSError:
            return
        reply = answer(packet)
        if reply:
            sock.sendto(reply, addr)


main()
