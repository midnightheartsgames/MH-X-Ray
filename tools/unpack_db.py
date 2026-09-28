import argparse
import pathlib
import struct
import sys
import zlib

COMPRESS_MARK = 1 << 31
LZHUF_N = 4096
LZHUF_F = 60
LZHUF_THRESHOLD = 2
LZHUF_N_CHAR = 256 - LZHUF_THRESHOLD + LZHUF_F
LZHUF_T = LZHUF_N_CHAR * 2 - 1
LZHUF_R = LZHUF_T - 1
LZHUF_MAX_FREQ = 0x4000
P_LEN = [3] + [4] * 3 + [5] * 8 + [6] * 12 + [7] * 24 + [8] * 16
P_CODE = [
    0x00, 0x20, 0x30, 0x40, 0x50, 0x58, 0x60, 0x68, 0x70, 0x78, 0x80, 0x88, 0x90, 0x94, 0x98, 0x9C,
    0xA0, 0xA4, 0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xBC, 0xC0, 0xC2, 0xC4, 0xC6, 0xC8, 0xCA, 0xCC, 0xCE,
    0xD0, 0xD2, 0xD4, 0xD6, 0xD8, 0xDA, 0xDC, 0xDE, 0xE0, 0xE2, 0xE4, 0xE6, 0xE8, 0xEA, 0xEC, 0xEE,
    0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF,
]


def decoding_tables():
    codes = [0] * 256
    lengths = [0] * 256
    for position, (length, code) in enumerate(zip(P_LEN, P_CODE)):
        for value in range(code, code + (1 << (8 - length))):
            codes[value] = position
            lengths[value] = length
    return codes, lengths


D_CODE, D_LEN = decoding_tables()


HEADER_KEYS = {
    "russian": (2048, 20091958, 20031955),
    "worldwide": (1024, 6011979, 24031979),
}
MAX_HEADER_SIZE = 64 * 1024 * 1024


class Random32:
    def __init__(self, seed):
        self.seed = seed

    def next(self, limit):
        self.seed = (0x08088405 * self.seed + 1) & 0xFFFFFFFF
        return (self.seed * limit) >> 32


def alphabet_back(iterations, table_seed):
    alphabet = list(range(256))
    generator = Random32(table_seed)
    for _ in range(iterations):
        first = generator.next(256)
        second = generator.next(256)
        while first == second:
            second = generator.next(256)
        alphabet[first], alphabet[second] = alphabet[second], alphabet[first]
    back = [0] * 256
    for index, value in enumerate(alphabet):
        back[value] = index
    return back


def decrypt_header(data, key):
    iterations, table_seed, encrypt_seed = HEADER_KEYS[key]
    back = alphabet_back(iterations, table_seed)
    generator = Random32(encrypt_seed)
    return bytes(back[value ^ (generator.next(256) & 0xFF)] for value in data)


def plausible_header(data):
    return len(data) >= 4 and 0 < struct.unpack_from("<I", data, 0)[0] <= MAX_HEADER_SIZE


class LzhufDecoder:
    def __init__(self, data):
        self.data = data
        self.position = 0
        self.bit_buffer = 0
        self.bit_count = 0
        self.freq = [0] * (LZHUF_T + 1)
        self.parent = [0] * (LZHUF_T + LZHUF_N_CHAR + 1)
        self.son = [0] * LZHUF_T

    def next_byte(self):
        if self.position >= len(self.data):
            return 0
        value = self.data[self.position]
        self.position += 1
        return value

    def fill(self):
        while self.bit_count <= 8:
            self.bit_buffer |= self.next_byte() << (8 - self.bit_count)
            self.bit_buffer &= 0xFFFF
            self.bit_count += 8

    def get_bit(self):
        self.fill()
        value = self.bit_buffer
        self.bit_buffer = (self.bit_buffer << 1) & 0xFFFF
        self.bit_count -= 1
        return (value & 0x8000) >> 15

    def get_byte(self):
        self.fill()
        value = self.bit_buffer
        self.bit_buffer = (self.bit_buffer << 8) & 0xFFFF
        self.bit_count -= 8
        return (value & 0xFF00) >> 8

    def start(self):
        for index in range(LZHUF_N_CHAR):
            self.freq[index] = 1
            self.son[index] = index + LZHUF_T
            self.parent[index + LZHUF_T] = index
        left, node = 0, LZHUF_N_CHAR
        while node <= LZHUF_R:
            self.freq[node] = self.freq[left] + self.freq[left + 1]
            self.son[node] = left
            self.parent[left] = self.parent[left + 1] = node
            left += 2
            node += 1
        self.freq[LZHUF_T] = 0xFFFF
        self.parent[LZHUF_R] = 0

    def rebuild(self):
        freq, son, parent = self.freq, self.son, self.parent
        leaf = 0
        for index in range(LZHUF_T):
            if son[index] >= LZHUF_T:
                freq[leaf] = (freq[index] + 1) // 2
                son[leaf] = son[index]
                leaf += 1
        left, node = 0, LZHUF_N_CHAR
        while node < LZHUF_T:
            total = freq[left] + freq[left + 1]
            freq[node] = total
            slot = node - 1
            while total < freq[slot]:
                slot -= 1
            slot += 1
            freq[slot + 1:node + 1] = freq[slot:node]
            freq[slot] = total
            son[slot + 1:node + 1] = son[slot:node]
            son[slot] = left
            left += 2
            node += 1
        for index in range(LZHUF_T):
            child = son[index]
            if child >= LZHUF_T:
                parent[child] = index
            else:
                parent[child] = parent[child + 1] = index

    def update(self, code):
        freq, son, parent = self.freq, self.son, self.parent
        if freq[LZHUF_R] == LZHUF_MAX_FREQ:
            self.rebuild()
        node = parent[code + LZHUF_T]
        while True:
            freq[node] += 1
            count = freq[node]
            if count > freq[node + 1]:
                swap = node + 1
                while count > freq[swap + 1]:
                    swap += 1
                freq[node] = freq[swap]
                freq[swap] = count
                first = son[node]
                parent[first] = swap
                if first < LZHUF_T:
                    parent[first + 1] = swap
                second = son[swap]
                son[swap] = first
                parent[second] = node
                if second < LZHUF_T:
                    parent[second + 1] = node
                son[node] = second
                node = swap
            node = parent[node]
            if node == 0:
                break

    def decode_char(self):
        node = self.son[LZHUF_R]
        while node < LZHUF_T:
            node = self.son[node + self.get_bit()]
        node -= LZHUF_T
        self.update(node)
        return node

    def decode_position(self):
        value = self.get_byte()
        upper = D_CODE[value] << 6
        extra = D_LEN[value] - 2
        for _ in range(extra):
            value = (value << 1) + self.get_bit()
        return upper | (value & 0x3F)

    def decode(self):
        size = struct.unpack_from("<I", self.data, 0)[0]
        self.position = 4
        if size == 0:
            return b""
        self.start()
        text = bytearray(b" " * (LZHUF_N + LZHUF_F))
        cursor = LZHUF_N - LZHUF_F
        output = bytearray()
        while len(output) < size:
            char = self.decode_char()
            if char < 256:
                output.append(char)
                text[cursor] = char
                cursor = (cursor + 1) & (LZHUF_N - 1)
            else:
                start = (cursor - self.decode_position() - 1) & (LZHUF_N - 1)
                for offset in range(char - 255 + LZHUF_THRESHOLD):
                    value = text[(start + offset) & (LZHUF_N - 1)]
                    output.append(value)
                    text[cursor] = value
                    cursor = (cursor + 1) & (LZHUF_N - 1)
        return bytes(output)


def lzo1x_decompress(source, size):
    output = bytearray()
    ip = 0

    def copy_match(distance, length):
        start = len(output) - distance
        if start < 0:
            raise ValueError("lzo: match before output start")
        for index in range(length):
            output.append(output[start + index])

    def literal_run_length(value, base):
        nonlocal ip
        if value == 0:
            while source[ip] == 0:
                value += 255
                ip += 1
            value += base + source[ip]
            ip += 1
        return value

    state = "first"
    token = 0
    if source[0] > 17:
        token = source[0] - 17
        ip = 1
        if token < 4:
            state = "match_next"
        else:
            output += source[ip:ip + token]
            ip += token
            state = "first_literal_run"
    while True:
        if state == "first":
            token = source[ip]
            ip += 1
            if token >= 16:
                state = "match"
                continue
            token = literal_run_length(token, 15)
            output += source[ip:ip + token + 3]
            ip += token + 3
            state = "first_literal_run"
        if state == "first_literal_run":
            token = source[ip]
            ip += 1
            if token >= 16:
                state = "match"
            else:
                distance = 1 + 0x0800 + (token >> 2) + (source[ip] << 2)
                ip += 1
                copy_match(distance, 3)
                state = "match_done"
        if state == "match":
            if token >= 64:
                distance = 1 + ((token >> 2) & 7) + (source[ip] << 3)
                ip += 1
                copy_match(distance, (token >> 5) - 1 + 2)
            elif token >= 32:
                length = literal_run_length(token & 31, 31)
                distance = 1 + (source[ip] >> 2) + (source[ip + 1] << 6)
                ip += 2
                copy_match(distance, length + 2)
            elif token >= 16:
                length = literal_run_length(token & 7, 7)
                distance = ((token & 8) << 11) + (source[ip] >> 2) + (source[ip + 1] << 6)
                ip += 2
                if distance == 0:
                    break
                copy_match(distance + 0x4000, length + 2)
            else:
                distance = 1 + (token >> 2) + (source[ip] << 2)
                ip += 1
                copy_match(distance, 2)
            state = "match_done"
        if state == "match_done":
            token = source[ip - 2] & 3
            if token == 0:
                state = "first"
                continue
            state = "match_next"
        if state == "match_next":
            output += source[ip:ip + token]
            ip += token
            token = source[ip]
            ip += 1
            state = "match"
    if len(output) != size:
        raise ValueError(f"lzo: expected {size} bytes, got {len(output)}")
    return bytes(output)


def read_header(archive):
    offset = 0
    while offset + 8 <= len(archive):
        chunk_id, chunk_size = struct.unpack_from("<II", archive, offset)
        offset += 8
        if chunk_id & ~COMPRESS_MARK == 1:
            data = archive[offset:offset + chunk_size]
            if not chunk_id & COMPRESS_MARK:
                return data
            for key in HEADER_KEYS:
                decrypted = decrypt_header(data, key)
                if plausible_header(decrypted):
                    return LzhufDecoder(decrypted).decode()
            raise ValueError("header does not match any known key")
        offset += chunk_size
    raise ValueError("archive has no header chunk")


def entries(header):
    offset = 0
    while offset < len(header):
        record_size = struct.unpack_from("<H", header, offset)[0]
        offset += 2
        size_real, size_compressed, crc = struct.unpack_from("<III", header, offset)
        name = header[offset + 12:offset + record_size - 4].decode("cp1251")
        pointer = struct.unpack_from("<I", header, offset + record_size - 4)[0]
        offset += record_size
        yield name, pointer, size_real, size_compressed, crc


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("--game", default=str(pathlib.Path(__file__).resolve().parents[2] / "S.T.A.L.K.E.R. Shadow of Chernobyl"))
    parser.add_argument("--ext", default="script,ltx,xml")
    arguments = parser.parse_args()
    extensions = tuple("." + value.strip().lower() for value in arguments.ext.split(","))
    output = pathlib.Path(arguments.output)
    archives = sorted(pathlib.Path(arguments.game).glob("gamedata.db*"))
    written = failed = 0
    for archive_path in archives:
        archive = archive_path.read_bytes()
        for name, pointer, size_real, size_compressed, crc in entries(read_header(archive)):
            if not name.lower().endswith(extensions) or size_real == 0:
                continue
            data = archive[pointer:pointer + size_compressed]
            if size_real != size_compressed:
                data = lzo1x_decompress(data, size_real)
            if zlib.crc32(data) != crc:
                print(f"crc mismatch: {archive_path.name}: {name}", file=sys.stderr)
                failed += 1
                continue
            target = output / name.replace("\\", "/")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            written += 1
        print(f"{archive_path.name}: done")
    print(f"written {written}, crc failures {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
