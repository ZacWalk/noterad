// util.cpp — AES-256, SHA-256, and UTF-8/UTF-16 string conversions

#include "pch.h"
#include "util.h"


static constexpr uint8_t aes_F(const uint8_t x)
{
	return x << 1 ^ (x >> 7 & 1) * 0x1b;
}

static constexpr uint8_t aes_FD(const uint8_t x)
{
	return x >> 1 ^ (x & 1 ? 0x8d : 0);
}

const uint8_t sbox[256] = {
	0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5,
	0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
	0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc,
	0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a,
	0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
	0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b,
	0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85,
	0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
	0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17,
	0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88,
	0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
	0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9,
	0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6,
	0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
	0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94,
	0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68,
	0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16
};
const uint8_t sboxinv[256] = {
	0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38,
	0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
	0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87,
	0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
	0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d,
	0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
	0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2,
	0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
	0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16,
	0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
	0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda,
	0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
	0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a,
	0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
	0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02,
	0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
	0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea,
	0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
	0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85,
	0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
	0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89,
	0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
	0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20,
	0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
	0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31,
	0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
	0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d,
	0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
	0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0,
	0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
	0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26,
	0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d
};

#define rj_sbox(x)     sbox[(x)]
#define rj_sbox_inv(x) sboxinv[(x)]


static uint8_t rj_xtime(const uint8_t x)
{
	return x & 0x80 ? x << 1 ^ 0x1b : x << 1;
}

static void aes_subBytes(uint8_t* buf)
{
	uint8_t i = 16;

	while (i--) buf[i] = rj_sbox(buf[i]);
}

static void aes_subBytes_inv(uint8_t* buf)
{
	uint8_t i = 16;

	while (i--) buf[i] = rj_sbox_inv(buf[i]);
}

static void aes_addRoundKey(uint8_t* buf, const uint8_t* key)
{
	uint8_t i = 16;

	while (i--) buf[i] ^= key[i];
}

static void aes_addRoundKey_cpy(uint8_t* buf, const uint8_t* key, uint8_t* cpk)
{
	uint8_t i = 16;

	while (i--) buf[i] ^= cpk[i] = key[i], cpk[16 + i] = key[16 + i];
}


static void aes_shiftRows(uint8_t* buf)
{
	uint8_t i = buf[1];
	buf[1] = buf[5];
	buf[5] = buf[9];
	buf[9] = buf[13];
	buf[13] = i;
	i = buf[10];
	buf[10] = buf[2];
	buf[2] = i;
	uint8_t j = buf[3];
	buf[3] = buf[15];
	buf[15] = buf[11];
	buf[11] = buf[7];
	buf[7] = j;
	j = buf[14];
	buf[14] = buf[6];
	buf[6] = j;
} /* aes_shiftRows */

static void aes_shiftRows_inv(uint8_t* buf)
{
	uint8_t i = buf[1];
	buf[1] = buf[13];
	buf[13] = buf[9];
	buf[9] = buf[5];
	buf[5] = i;
	i = buf[2];
	buf[2] = buf[10];
	buf[10] = i;
	uint8_t j = buf[3];
	buf[3] = buf[7];
	buf[7] = buf[11];
	buf[11] = buf[15];
	buf[15] = j;
	j = buf[6];
	buf[6] = buf[14];
	buf[14] = j;
} /* aes_shiftRows_inv */

static void aes_mixColumns(uint8_t* buf)
{
	for (uint8_t i = 0; i < 16; i += 4)
	{
		const uint8_t a = buf[i];
		const uint8_t b = buf[i + 1];
		const uint8_t c = buf[i + 2];
		const uint8_t d = buf[i + 3];
		const uint8_t e = a ^ b ^ c ^ d;
		buf[i] ^= e ^ rj_xtime(a ^ b);
		buf[i + 1] ^= e ^ rj_xtime(b ^ c);
		buf[i + 2] ^= e ^ rj_xtime(c ^ d);
		buf[i + 3] ^= e ^ rj_xtime(d ^ a);
	}
} /* aes_mixColumns */

static void aes_mixColumns_inv(uint8_t* buf)
{
	for (uint8_t i = 0; i < 16; i += 4)
	{
		const uint8_t a = buf[i];
		const uint8_t b = buf[i + 1];
		const uint8_t c = buf[i + 2];
		const uint8_t d = buf[i + 3];
		const uint8_t e = a ^ b ^ c ^ d;
		const uint8_t z = rj_xtime(e);
		const uint8_t x = e ^ rj_xtime(rj_xtime(z ^ a ^ c));
		const uint8_t y = e ^ rj_xtime(rj_xtime(z ^ b ^ d));
		buf[i] ^= x ^ rj_xtime(a ^ b);
		buf[i + 1] ^= y ^ rj_xtime(b ^ c);
		buf[i + 2] ^= x ^ rj_xtime(c ^ d);
		buf[i + 3] ^= y ^ rj_xtime(d ^ a);
	}
} /* aes_mixColumns_inv */

static void aes_expandEncKey(uint8_t* k, uint8_t* rc)
{
	uint8_t i;

	k[0] ^= rj_sbox(k[29]) ^ *rc;
	k[1] ^= rj_sbox(k[30]);
	k[2] ^= rj_sbox(k[31]);
	k[3] ^= rj_sbox(k[28]);
	*rc = aes_F(*rc);

	for (i = 4; i < 16; i += 4)
		k[i] ^= k[i - 4], k[i + 1] ^= k[i - 3],
			k[i + 2] ^= k[i - 2], k[i + 3] ^= k[i - 1];
	k[16] ^= rj_sbox(k[12]);
	k[17] ^= rj_sbox(k[13]);
	k[18] ^= rj_sbox(k[14]);
	k[19] ^= rj_sbox(k[15]);

	for (i = 20; i < 32; i += 4)
		k[i] ^= k[i - 4], k[i + 1] ^= k[i - 3],
			k[i + 2] ^= k[i - 2], k[i + 3] ^= k[i - 1];
} /* aes_expandEncKey */

static void aes_expandDecKey(uint8_t* k, uint8_t* rc)
{
	uint8_t i;

	for (i = 28; i > 16; i -= 4)
		k[i + 0] ^= k[i - 4], k[i + 1] ^= k[i - 3],
			k[i + 2] ^= k[i - 2], k[i + 3] ^= k[i - 1];

	k[16] ^= rj_sbox(k[12]);
	k[17] ^= rj_sbox(k[13]);
	k[18] ^= rj_sbox(k[14]);
	k[19] ^= rj_sbox(k[15]);

	for (i = 12; i > 0; i -= 4)
		k[i + 0] ^= k[i - 4], k[i + 1] ^= k[i - 3],
			k[i + 2] ^= k[i - 2], k[i + 3] ^= k[i - 1];

	*rc = aes_FD(*rc);
	k[0] ^= rj_sbox(k[29]) ^ *rc;
	k[1] ^= rj_sbox(k[30]);
	k[2] ^= rj_sbox(k[31]);
	k[3] ^= rj_sbox(k[28]);
} /* aes_expandDecKey */


aes256::aes256(const std::span<const uint8_t> k)
{
	if (k.size() < 32)
		throw std::invalid_argument("AES-256 key must be at least 32 bytes");
	uint8_t rcon = 1;

	std::copy_n(k.data(), sizeof(key), key);
	std::copy_n(k.data(), sizeof(key), enckey);
	std::copy_n(k.data(), sizeof(key), deckey);
	for (uint8_t i = 8; --i;) aes_expandEncKey(deckey, &rcon);
} /* aes256_init */

aes256::~aes256()
{
	volatile uint8_t* p = key;
	for (size_t i = 0; i < sizeof(key); ++i) p[i] = 0;
	p = enckey;
	for (size_t i = 0; i < sizeof(enckey); ++i) p[i] = 0;
	p = deckey;
	for (size_t i = 0; i < sizeof(deckey); ++i) p[i] = 0;
} /* aes256_done */

void aes256::encrypt_ecb(uint8_t* buf)
{
	uint8_t i, rcon;

	aes_addRoundKey_cpy(buf, enckey, key);
	for (i = 1, rcon = 1; i < 14; ++i)
	{
		aes_subBytes(buf);
		aes_shiftRows(buf);
		aes_mixColumns(buf);
		if (i & 1) aes_addRoundKey(buf, &key[16]);
		else aes_expandEncKey(key, &rcon), aes_addRoundKey(buf, key);
	}
	aes_subBytes(buf);
	aes_shiftRows(buf);
	aes_expandEncKey(key, &rcon);
	aes_addRoundKey(buf, key);
} /* aes256_encrypt */

void aes256::decrypt_ecb(uint8_t* buf)
{
	uint8_t i, rcon;

	aes_addRoundKey_cpy(buf, deckey, key);
	aes_shiftRows_inv(buf);
	aes_subBytes_inv(buf);

	for (i = 14, rcon = 0x80; --i;)
	{
		if (i & 1)
		{
			aes_expandDecKey(key, &rcon);
			aes_addRoundKey(buf, &key[16]);
		}
		else aes_addRoundKey(buf, key);
		aes_mixColumns_inv(buf);
		aes_shiftRows_inv(buf);
		aes_subBytes_inv(buf);
	}
	aes_addRoundKey(buf, key);
} /* aes256_decrypt */


#define GET_UINT32(n,b,i)                       \
{                                               \
    (n) = ( (uint32_t) (b)[(i)    ] << 24 )       \
        | ( (uint32_t) (b)[(i) + 1] << 16 )       \
        | ( (uint32_t) (b)[(i) + 2] <<  8 )       \
        | ( (uint32_t) (b)[(i) + 3]       );      \
}

#define PUT_UINT32(n,b,i)                       \
{                                               \
    (b)[(i)    ] = (uint8_t) ( (n) >> 24 );       \
    (b)[(i) + 1] = (uint8_t) ( (n) >> 16 );       \
    (b)[(i) + 2] = (uint8_t) ( (n) >>  8 );       \
    (b)[(i) + 3] = (uint8_t) ( (n)       );       \
}


sha256::sha256()
{
	total[0] = 0;
	total[1] = 0;

	state[0] = 0x6A09E667;
	state[1] = 0xBB67AE85;
	state[2] = 0x3C6EF372;
	state[3] = 0xA54FF53A;
	state[4] = 0x510E527F;
	state[5] = 0x9B05688C;
	state[6] = 0x1F83D9AB;
	state[7] = 0x5BE0CD19;
}

void sha256::process(const uint8_t data[64])
{
	uint32_t temp1, temp2, W[64];
	uint32_t A, B, C, D, E, F, G, H;

	GET_UINT32(W[0], data, 0);
	GET_UINT32(W[1], data, 4);
	GET_UINT32(W[2], data, 8);
	GET_UINT32(W[3], data, 12);
	GET_UINT32(W[4], data, 16);
	GET_UINT32(W[5], data, 20);
	GET_UINT32(W[6], data, 24);
	GET_UINT32(W[7], data, 28);
	GET_UINT32(W[8], data, 32);
	GET_UINT32(W[9], data, 36);
	GET_UINT32(W[10], data, 40);
	GET_UINT32(W[11], data, 44);
	GET_UINT32(W[12], data, 48);
	GET_UINT32(W[13], data, 52);
	GET_UINT32(W[14], data, 56);
	GET_UINT32(W[15], data, 60);

#define  SHR(x,n) ((x & 0xFFFFFFFF) >> n)
#define ROTR(x,n) (SHR(x,n) | (x << (32 - n)))

#define S0(x) (ROTR(x, 7) ^ ROTR(x,18) ^  SHR(x, 3))
#define S1(x) (ROTR(x,17) ^ ROTR(x,19) ^  SHR(x,10))

#define S2(x) (ROTR(x, 2) ^ ROTR(x,13) ^ ROTR(x,22))
#define S3(x) (ROTR(x, 6) ^ ROTR(x,11) ^ ROTR(x,25))

#define F0(x,y,z) ((x & y) | (z & (x | y)))
#define F1(x,y,z) (z ^ (x & (y ^ z)))

#define R(t)                                    \
(                                               \
    W[t] = S1(W[t -  2]) + W[t -  7] +          \
           S0(W[t - 15]) + W[t - 16]            \
)

#define P(a,b,c,d,e,f,g,h,x,K)                  \
        {                                               \
    temp1 = h + S3(e) + F1(e,f,g) + K + x;      \
    temp2 = S2(a) + F0(a,b,c);                  \
    d += temp1; h = temp1 + temp2;              \
        }

	A = state[0];
	B = state[1];
	C = state[2];
	D = state[3];
	E = state[4];
	F = state[5];
	G = state[6];
	H = state[7];

	P(A, B, C, D, E, F, G, H, W[0], 0x428A2F98);
	P(H, A, B, C, D, E, F, G, W[1], 0x71374491);
	P(G, H, A, B, C, D, E, F, W[2], 0xB5C0FBCF);
	P(F, G, H, A, B, C, D, E, W[3], 0xE9B5DBA5);
	P(E, F, G, H, A, B, C, D, W[4], 0x3956C25B);
	P(D, E, F, G, H, A, B, C, W[5], 0x59F111F1);
	P(C, D, E, F, G, H, A, B, W[6], 0x923F82A4);
	P(B, C, D, E, F, G, H, A, W[7], 0xAB1C5ED5);
	P(A, B, C, D, E, F, G, H, W[8], 0xD807AA98);
	P(H, A, B, C, D, E, F, G, W[9], 0x12835B01);
	P(G, H, A, B, C, D, E, F, W[10], 0x243185BE);
	P(F, G, H, A, B, C, D, E, W[11], 0x550C7DC3);
	P(E, F, G, H, A, B, C, D, W[12], 0x72BE5D74);
	P(D, E, F, G, H, A, B, C, W[13], 0x80DEB1FE);
	P(C, D, E, F, G, H, A, B, W[14], 0x9BDC06A7);
	P(B, C, D, E, F, G, H, A, W[15], 0xC19BF174);
	P(A, B, C, D, E, F, G, H, R(16), 0xE49B69C1);
	P(H, A, B, C, D, E, F, G, R(17), 0xEFBE4786);
	P(G, H, A, B, C, D, E, F, R(18), 0x0FC19DC6);
	P(F, G, H, A, B, C, D, E, R(19), 0x240CA1CC);
	P(E, F, G, H, A, B, C, D, R(20), 0x2DE92C6F);
	P(D, E, F, G, H, A, B, C, R(21), 0x4A7484AA);
	P(C, D, E, F, G, H, A, B, R(22), 0x5CB0A9DC);
	P(B, C, D, E, F, G, H, A, R(23), 0x76F988DA);
	P(A, B, C, D, E, F, G, H, R(24), 0x983E5152);
	P(H, A, B, C, D, E, F, G, R(25), 0xA831C66D);
	P(G, H, A, B, C, D, E, F, R(26), 0xB00327C8);
	P(F, G, H, A, B, C, D, E, R(27), 0xBF597FC7);
	P(E, F, G, H, A, B, C, D, R(28), 0xC6E00BF3);
	P(D, E, F, G, H, A, B, C, R(29), 0xD5A79147);
	P(C, D, E, F, G, H, A, B, R(30), 0x06CA6351);
	P(B, C, D, E, F, G, H, A, R(31), 0x14292967);
	P(A, B, C, D, E, F, G, H, R(32), 0x27B70A85);
	P(H, A, B, C, D, E, F, G, R(33), 0x2E1B2138);
	P(G, H, A, B, C, D, E, F, R(34), 0x4D2C6DFC);
	P(F, G, H, A, B, C, D, E, R(35), 0x53380D13);
	P(E, F, G, H, A, B, C, D, R(36), 0x650A7354);
	P(D, E, F, G, H, A, B, C, R(37), 0x766A0ABB);
	P(C, D, E, F, G, H, A, B, R(38), 0x81C2C92E);
	P(B, C, D, E, F, G, H, A, R(39), 0x92722C85);
	P(A, B, C, D, E, F, G, H, R(40), 0xA2BFE8A1);
	P(H, A, B, C, D, E, F, G, R(41), 0xA81A664B);
	P(G, H, A, B, C, D, E, F, R(42), 0xC24B8B70);
	P(F, G, H, A, B, C, D, E, R(43), 0xC76C51A3);
	P(E, F, G, H, A, B, C, D, R(44), 0xD192E819);
	P(D, E, F, G, H, A, B, C, R(45), 0xD6990624);
	P(C, D, E, F, G, H, A, B, R(46), 0xF40E3585);
	P(B, C, D, E, F, G, H, A, R(47), 0x106AA070);
	P(A, B, C, D, E, F, G, H, R(48), 0x19A4C116);
	P(H, A, B, C, D, E, F, G, R(49), 0x1E376C08);
	P(G, H, A, B, C, D, E, F, R(50), 0x2748774C);
	P(F, G, H, A, B, C, D, E, R(51), 0x34B0BCB5);
	P(E, F, G, H, A, B, C, D, R(52), 0x391C0CB3);
	P(D, E, F, G, H, A, B, C, R(53), 0x4ED8AA4A);
	P(C, D, E, F, G, H, A, B, R(54), 0x5B9CCA4F);
	P(B, C, D, E, F, G, H, A, R(55), 0x682E6FF3);
	P(A, B, C, D, E, F, G, H, R(56), 0x748F82EE);
	P(H, A, B, C, D, E, F, G, R(57), 0x78A5636F);
	P(G, H, A, B, C, D, E, F, R(58), 0x84C87814);
	P(F, G, H, A, B, C, D, E, R(59), 0x8CC70208);
	P(E, F, G, H, A, B, C, D, R(60), 0x90BEFFFA);
	P(D, E, F, G, H, A, B, C, R(61), 0xA4506CEB);
	P(C, D, E, F, G, H, A, B, R(62), 0xBEF9A3F7);
	P(B, C, D, E, F, G, H, A, R(63), 0xC67178F2);

	state[0] += A;
	state[1] += B;
	state[2] += C;
	state[3] += D;
	state[4] += E;
	state[5] += F;
	state[6] += G;
	state[7] += H;
}

void sha256::update(const uint8_t* input, size_t length)
{
	if (!length) return;

	uint32_t left = total[0] & 0x3F;
	const uint32_t fill = 64 - left;

	const auto prev_total0 = total[0];
	total[0] += static_cast<uint32_t>(length);
	total[0] &= 0xFFFFFFFF;

	if (total[0] < prev_total0)
		total[1]++;

	total[1] += static_cast<uint32_t>(length >> 32);

	if (left && length >= fill)
	{
		std::copy_n(input, fill, buffer + left);
		process(buffer);
		length -= fill;
		input += fill;
		left = 0;
	}

	while (length >= 64)
	{
		process(input);
		length -= 64;
		input += 64;
	}

	if (length)
	{
		std::copy_n(input, length, buffer + left);
	}
}

static const uint8_t sha256_padding[64] =
{
	0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void sha256::finish(uint8_t digest[32])
{
	uint8_t msglen[8];

	const uint32_t high = total[0] >> 29
		| total[1] << 3;
	const uint32_t low = total[0] << 3;

	PUT_UINT32(high, msglen, 0);
	PUT_UINT32(low, msglen, 4);

	const uint32_t last = total[0] & 0x3F;
	const uint32_t padn = last < 56 ? 56 - last : 120 - last;

	update(sha256_padding, padn);
	update(msglen, 8);

	PUT_UINT32(state[0], digest, 0);
	PUT_UINT32(state[1], digest, 4);
	PUT_UINT32(state[2], digest, 8);
	PUT_UINT32(state[3], digest, 12);
	PUT_UINT32(state[4], digest, 16);
	PUT_UINT32(state[5], digest, 20);
	PUT_UINT32(state[6], digest, 24);
	PUT_UINT32(state[7], digest, 28);
}

#undef GET_UINT32
#undef PUT_UINT32
#undef SHR
#undef ROTR
#undef S0
#undef S1
#undef S2
#undef S3
#undef F0
#undef F1
#undef R
#undef P

// ── String utilities ────────────────────────────────────────────────────────────

size_t find_in_text(const std::string_view text, const std::string_view pattern, const bool match_case)
{
	if (text.empty()) return std::string_view::npos;
	if (pattern.empty()) return std::string_view::npos;

	const auto text_len = text.size();
	const auto pat_len = pattern.size();

	if (pat_len > text_len) return std::string_view::npos;

	for (size_t pos = 0; pos <= text_len - pat_len; ++pos)
	{
		bool found = true;
		for (size_t j = 0; j < pat_len; ++j)
		{
			const auto tc = text[pos + j];
			const auto pc = pattern[j];
			if (tc != pc && (match_case || pf::to_lower(tc) != pf::to_lower(pc)))
			{
				found = false;
				break;
			}
		}
		if (found) return pos;
	}
	return std::string_view::npos;
}

std::string combine(const std::vector<std::string>& lines, const std::string_view endl)
{
	return join(lines, [](const std::string& s) -> std::string_view { return s; }, endl);
}

std::string combine(const std::vector<std::string_view>& lines, const std::string_view endl)
{
	return join(lines, [](const std::string_view& s) -> std::string_view { return s; }, endl);
}

std::string replace(const std::string_view s, const std::string_view find,
                    const std::string_view replacement)
{
	if (find.empty())
		return std::string(s);

	std::string result(s);
	size_t pos = 0;
	const auto findLength = find.size();
	const auto replacementLength = replacement.size();

	while ((pos = result.find(find, pos)) != std::string::npos)
	{
		result.replace(pos, findLength, replacement);
		pos += replacementLength;
	}

	return result;
}


// ── Encoding / hashing ─────────────────────────────────────────────────────────

std::string to_base64(const std::span<const uint8_t> input)
{
	static constexpr std::string_view base64_chars =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789+/";

	std::string ret;
	ret.reserve((input.size() + 2) / 3 * 4);
	int i = 0;
	int j = 0;
	uint8_t char_array_3[3];
	uint8_t char_array_4[4];

	for (const auto byte : input)
	{
		char_array_3[i++] = byte;
		if (i == 3)
		{
			char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
			char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
			char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
			char_array_4[3] = char_array_3[2] & 0x3f;

			for (i = 0; i < 4; i++)
				ret += base64_chars[char_array_4[i]];
			i = 0;
		}
	}

	if (i)
	{
		for (j = i; j < 3; j++)
			char_array_3[j] = '\0';

		char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
		char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
		char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
		char_array_4[3] = char_array_3[2] & 0x3f;

		for (j = 0; j < i + 1; j++)
			ret += base64_chars[char_array_4[j]];

		while (i++ < 3)
			ret += '=';
	}

	return ret;
}

std::string to_hex(const std::span<const uint8_t> src)
{
	static constexpr char digits[] = "0123456789abcdef";
	std::string result;
	result.reserve(src.size() * 2);

	for (const auto ch : src)
	{
		result += digits[(ch & 0xf0) >> 4];
		result += digits[ch & 0x0f];
	}

	return result;
}

std::vector<uint8_t> hex_to_data(const std::string_view text)
{
	std::vector<uint8_t> result;
	result.reserve((text.size() + 1) / 2);

	auto high_part = text.size() % 2 == 0;

	if (!high_part)
		result.push_back(0);

	for (const auto c : text)
	{
		const auto nibble = char_to_hex(c);
		if (nibble < 0) continue;

		if (high_part)
			result.push_back(static_cast<uint8_t>(0x10 * nibble));
		else
			result.back() += static_cast<uint8_t>(nibble);

		high_part = !high_part;
	}

	return result;
}

std::vector<uint8_t> calc_sha256(const std::string_view text)
{
	uint8_t result[32];

	sha256 h;
	h.update(reinterpret_cast<const uint8_t*>(text.data()), text.size());
	h.finish(result);

	return {result, result + 32};
}
