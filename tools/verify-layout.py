#!/usr/bin/env python3
# Check the arithmetic that lib/crc/x86/crc32c-mixed.S relies on, for every
# length it can be given: the division by multiplication, the way a length is
# divided into V, lanes and F, the loop counts, and the table indices.

BYTES_PER_ITER = 224
TOP = 255
MIN_LEN = 512
MAX_LEN = 4096
MULT, SHIFT = 74899, 24

# The division trick, over the whole range where the product fits in 32 bits.
limit = 0
while (limit * MULT) < 2**32 and (limit * MULT) >> SHIFT == limit // BYTES_PER_ITER:
    limit += 1
print(f'(x * {MULT}) >> {SHIFT} == x // {BYTES_PER_ITER} for all x < {limit}')
assert limit > MAX_LEN - 16

max_index = max_m = max_q = 0
pre_rounds = set()
for length in range(MIN_LEN, MAX_LEN + 1, 8):
    x = length - 16
    n = (x * MULT) >> SHIFT
    r = (x - BYTES_PER_ITER * n) // 8
    e, t = r // 4, r % 4
    q = 3 * n + e
    m = 4 * q + t

    # The pieces add up to the whole.
    assert 128 * n + 8 * (4 * q + t) + 16 == length, length
    # The main loop runs at least once, after 3 to 9 rounds on the lanes.
    assert n - 1 >= 1 and 3 <= e + 3 <= 9, length
    pre_rounds.add(e + 3)

    # Multipliers used: M(m + 16) ... M(m + 1), M(q), M(2q), M(3q), M(16), M(15).
    used = list(range(m + 1, m + 17)) + [q, 2 * q, 3 * q, 15, 16]
    assert 0 <= min(used) and max(used) <= TOP, (length, max(used))
    max_index = max(max_index, max(used))
    max_m, max_q = max(max_m, m), max(max_q, q)

print(f'lengths {MIN_LEN}..{MAX_LEN}: largest m = {max_m}, largest q = {max_q}, '
      f'largest multiplier index = {max_index} (table has 0..{TOP})')
print(f'rounds before the main loop: {min(pre_rounds)}..{max(pre_rounds)}')

# The C glue takes len % 8 bytes off the front, then splits what is left:
# every piece is a legal length.
def pieces(length):
    out = []
    length -= length % 8
    while length:
        n = length
        if n > MAX_LEN:
            n = min((n // 2 + 63) // 64 * 64, MAX_LEN)
        out.append(n)
        length -= n
    return out

worst = MAX_LEN
for length in range(MIN_LEN, 300000):
    ps = pieces(length)
    assert sum(ps) == length - length % 8
    assert all(MIN_LEN <= p <= MAX_LEN and p % 8 == 0 for p in ps), (length, ps)
    assert all(p % 64 == 0 for p in ps[:-1]), (length, ps)
    worst = min(worst, min(ps))
print(f'glue: every piece of every length up to 300000 is legal; smallest piece = {worst}')
