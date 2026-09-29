import math
import struct
import sys


def view_matrix(eye, yaw_degrees, pitch_degrees):
    yaw = math.radians(yaw_degrees)
    pitch = math.radians(pitch_degrees)
    direction = (math.sin(yaw) * math.cos(pitch), math.sin(pitch), math.cos(yaw) * math.cos(pitch))
    world_up = (0.0, 1.0, 0.0)
    dot = sum(a * b for a, b in zip(world_up, direction))
    up = tuple(world_up[i] - direction[i] * dot for i in range(3))
    length = math.sqrt(sum(c * c for c in up))
    up = tuple(c / length for c in up)
    right = (up[1] * direction[2] - up[2] * direction[1],
             up[2] * direction[0] - up[0] * direction[2],
             up[0] * direction[1] - up[1] * direction[0])

    def dot3(a, b):
        return sum(x * y for x, y in zip(a, b))

    return [right[0], up[0], direction[0], 0.0,
            right[1], up[1], direction[1], 0.0,
            right[2], up[2], direction[2], 0.0,
            -dot3(eye, right), -dot3(eye, up), -dot3(eye, direction), 1.0]


def main():
    if len(sys.argv) < 3:
        print('usage: make_demo.py <file.xrdemo> <x,y,z,yaw,pitch,seconds> [...]')
        return 1
    keys = []
    for spec in sys.argv[2:]:
        x, y, z, yaw, pitch, seconds = spec.split(',')
        matrix = view_matrix((float(x), float(y), float(z)), float(yaw), float(pitch))
        keys.extend([matrix] * int(seconds))
    with open(sys.argv[1], 'wb') as output:
        for matrix in keys:
            output.write(struct.pack('<16f', *matrix))
    print(f'{sys.argv[1]}: {len(keys)} keys')
    return 0


sys.exit(main())
