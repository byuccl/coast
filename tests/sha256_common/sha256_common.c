// DBL_INT_ADD treats two unsigned ints a and b as one 64-bit integer and adds c
#define DBL_INT_ADD(a, b, c)                                                   \
    if (a > 0xffffffff - (c))                                                  \
        ++b;                                                                   \
    a += c;
#define ROTRIGHT(a, b) (((a) >> (b)) | ((a) << (32 - (b))))

uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

unsigned char data[64];
uint32_t state[8];
uint32_t bitlen[2];

unsigned char hashGlbl[32];

void *sha_memset(void *s, int32_t c, int32_t n) {
    char *p = s;

    c = c & 0xFF;
    while (n--)
        *p++ = c;

    return s;
}

void sha_memcpy(void *dest, void *src, int32_t n) {
    char *d = dest;
    char *s = src;

    while (n--)
        *d++ = *s++;
}

uint32_t sha_strlen(char *c) {
    int32_t l = 0;
    while (*c != '\0') {
        c++;
        l++;
    }
    return l;
}

void sha256_transform(uint32_t ctx_state[], unsigned char data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, s, t1, t2, m[64];
    uint32_t ep0, ep1, ch, maj, sig0, sig1;

    uint32_t temp;

    for (i = 0, j = 0; i < 16; ++i, j += 4) {
        temp = (((uint32_t)data[j]) << 24);
        temp |= (((uint32_t)data[j + 1]) << 16);
        temp |= (data[j + 2] << 8);
        temp |= (data[j + 3]);
        m[i] = temp;
    }

    for (; i < 64; ++i) {
        s = m[i - 2];
        sig1 = ROTRIGHT(s, 17);
        sig1 ^= ROTRIGHT(s, 19);
        sig1 ^= s >> 10;

        s = m[i - 15];
        sig0 = ROTRIGHT(s, 7);
        sig0 ^= ROTRIGHT(s, 18);
        sig0 ^= s >> 3;

        temp = sig1;
        temp += m[i - 7];
        temp += sig0;
        temp += m[i - 16];
        m[i] = temp;
    }

    a = ctx_state[0];
    b = ctx_state[1];
    c = ctx_state[2];
    d = ctx_state[3];
    e = ctx_state[4];
    f = ctx_state[5];
    g = ctx_state[6];
    h = ctx_state[7];

    for (i = 0; i < 64; ++i) {
        ep0 = ROTRIGHT(a, 2);
        ep0 ^= ROTRIGHT(a, 13);
        ep0 ^= ROTRIGHT(a, 22);
        ep1 = ROTRIGHT(e, 6);
        ep1 ^= ROTRIGHT(e, 11);
        ep1 ^= ROTRIGHT(e, 25);
        ch = (e & f) ^ (~e & g);
        maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + ep1 + ch + k[i] + m[i];
        t2 = ep0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx_state[0] += a;
    ctx_state[1] += b;
    ctx_state[2] += c;
    ctx_state[3] += d;
    ctx_state[4] += e;
    ctx_state[5] += f;
    ctx_state[6] += g;
    ctx_state[7] += h;
}

void sha256_hash(unsigned char ctx_data[], uint32_t ctx_bitlen[],
                 uint32_t ctx_state[], unsigned char data[],
                 uint32_t len, unsigned char hash[]) {

    uint32_t ctx_datalen = 0;
    ctx_bitlen[0] = 0;
    ctx_bitlen[1] = 0;
    ctx_state[0] = 0x6a09e667;
    ctx_state[1] = 0xbb67ae85;
    ctx_state[2] = 0x3c6ef372;
    ctx_state[3] = 0xa54ff53a;
    ctx_state[4] = 0x510e527f;
    ctx_state[5] = 0x9b05688c;
    ctx_state[6] = 0x1f83d9ab;
    ctx_state[7] = 0x5be0cd19;

    uint32_t i;

    for (i = 0; i < len; ++i) {
        ctx_data[ctx_datalen] = data[i];
        (ctx_datalen)++;
        if (ctx_datalen == 64) {
            sha256_transform(ctx_state, ctx_data);
            DBL_INT_ADD(ctx_bitlen[0], ctx_bitlen[1], 512);
            ctx_datalen = 0;
        }
    }

    i = ctx_datalen;

    // Pad whatever data is left in the buffer.
    if (ctx_datalen < 56) {
        ctx_data[i++] = 0x80;
        while (i < 56)
            ctx_data[i++] = 0x00;
    } else {
        ctx_data[i++] = 0x80;
        while (i < 64)
            ctx_data[i++] = 0x00;
        sha256_transform(ctx_state, ctx_data);
        sha_memset(ctx_data, 0, 56);
    }

    // Append to the padding the total message's length in bits and transform.
    DBL_INT_ADD(ctx_bitlen[0], ctx_bitlen[1], ctx_datalen * 8);
    ctx_data[63] = ctx_bitlen[0];
    ctx_data[62] = ctx_bitlen[0] >> 8;
    ctx_data[61] = ctx_bitlen[0] >> 16;
    ctx_data[60] = ctx_bitlen[0] >> 24;
    ctx_data[59] = ctx_bitlen[1];
    ctx_data[58] = ctx_bitlen[1] >> 8;
    ctx_data[57] = ctx_bitlen[1] >> 16;
    ctx_data[56] = ctx_bitlen[1] >> 24;
    sha256_transform(ctx_state, ctx_data);

    // Since this implementation uses little endian byte ordering and SHA uses
    // big endian,
    // reverse all the bytes when copying the final state to the output hash.
    for (i = 0; i < 4; ++i) {
        hash[i] = (ctx_state[0] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 4] = (ctx_state[1] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 8] = (ctx_state[2] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 12] = (ctx_state[3] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 16] = (ctx_state[4] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 20] = (ctx_state[5] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 24] = (ctx_state[6] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 28] = (ctx_state[7] >> (24 - i * 8)) & 0x000000ff;
    }
}

void sha_run_test() {

    bitlen[0] = 0;
    bitlen[1] = 0;

    // Hash the input string
    sha256_hash(data, bitlen, state, hash_data, LEN, hashGlbl);
}

unsigned checkGolden() {
    int i = 0;
    for (i = 0; i < 32; i++) {
    	if (hashGlbl[i] != golden[i])
    		return 1;
    }
    return 0;
}
