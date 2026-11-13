/*
 * Mixed variants to build: V(K, S, R)
 *   K = zmm folding accumulators (0 = crc32q chains only)
 *   S = crc32q chains
 *   R = qwords per chain per loop iteration
 */
V(0, 6, 1)
V(2, 3, 3)
V(2, 4, 2)
V(2, 4, 3)
V(2, 5, 2)
V(2, 5, 3)
V(2, 6, 2)
V(3, 6, 4)
V(4, 4, 6)
V(4, 6, 6)
