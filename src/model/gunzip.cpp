#include "model/gunzip.h"

#include <array>
#include <string>

namespace rp {
namespace {

class Bits {
public:
    Bits(const uint8_t* data, size_t size) : d_(data), n_(size) {}

    unsigned bit() {
        if (pos_ >= n_) throw GunzipError("gzip: truncated data");
        const unsigned b = (d_[pos_] >> bitpos_) & 1u;
        if (++bitpos_ == 8) {
            bitpos_ = 0;
            ++pos_;
        }
        return b;
    }
    unsigned bits(int count) {
        unsigned v = 0;
        for (int k = 0; k < count; ++k) v |= bit() << k;
        return v;
    }
    void alignByte() {
        if (bitpos_) {
            bitpos_ = 0;
            ++pos_;
        }
    }
    size_t pos() const { return pos_; }
    void skip(size_t bytes) {
        if (bytes > n_ - pos_) throw GunzipError("gzip: truncated data");
        pos_ += bytes;
    }
    const uint8_t* at() const { return d_ + pos_; }

private:
    const uint8_t* d_;
    size_t n_;
    size_t pos_ = 0;
    int bitpos_ = 0;
};

// A canonical Huffman code: counts per length and the symbols in code order (as in zlib's puff).
struct Huffman {
    std::array<uint16_t, 16> count{};
    std::array<uint16_t, 320> symbol{};
};

void build(Huffman& h, const uint8_t* lengths, int n) {
    h.count.fill(0);
    for (int s = 0; s < n; ++s) h.count[lengths[s]]++;
    h.count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) throw GunzipError("gzip: over-subscribed code");
    }
    std::array<uint16_t, 16> offs{};
    for (int len = 1; len < 15; ++len) offs[len + 1] = uint16_t(offs[len] + h.count[len]);
    for (int s = 0; s < n; ++s) {
        if (lengths[s]) h.symbol[offs[lengths[s]]++] = uint16_t(s);
    }
}

int decode(Bits& in, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= int(in.bit());
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    throw GunzipError("gzip: bad Huffman code");
}

const uint16_t kLenBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                             35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t kLenExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                              193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint16_t kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void codes(Bits& in, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist, size_t maxOutput) {
    for (;;) {
        int sym = decode(in, lit);
        if (sym < 256) {
            if (out.size() >= maxOutput) throw GunzipError("gzip: output too large");
            out.push_back(uint8_t(sym));
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) throw GunzipError("gzip: bad length symbol");
            const size_t len = kLenBase[sym] + in.bits(kLenExtra[sym]);
            const int ds = decode(in, dist);
            if (ds >= 30) throw GunzipError("gzip: bad distance symbol");
            const size_t d = kDistBase[ds] + in.bits(kDistExtra[ds]);
            if (d > out.size()) throw GunzipError("gzip: distance too far back");
            if (len > maxOutput - out.size()) throw GunzipError("gzip: output too large");
            const size_t from = out.size() - d;
            for (size_t k = 0; k < len; ++k) out.push_back(out[from + k]);
        }
    }
}

void fixedTables(Huffman& lit, Huffman& dist) {
    uint8_t l[288];
    for (int s = 0; s < 144; ++s) l[s] = 8;
    for (int s = 144; s < 256; ++s) l[s] = 9;
    for (int s = 256; s < 280; ++s) l[s] = 7;
    for (int s = 280; s < 288; ++s) l[s] = 8;
    build(lit, l, 288);
    uint8_t d[30];
    for (auto& x : d) x = 5;
    build(dist, d, 30);
}

void dynamicTables(Bits& in, Huffman& lit, Huffman& dist) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int nlen = int(in.bits(5)) + 257, ndist = int(in.bits(5)) + 1, ncode = int(in.bits(4)) + 4;
    if (nlen > 286 || ndist > 30) throw GunzipError("gzip: bad code counts");
    uint8_t lengths[320] = {};
    for (int k = 0; k < ncode; ++k) lengths[order[k]] = uint8_t(in.bits(3));
    Huffman lencode;
    build(lencode, lengths, 19);
    int index = 0;
    while (index < nlen + ndist) {
        int sym = decode(in, lencode);
        if (sym < 16) {
            lengths[index++] = uint8_t(sym);
            continue;
        }
        uint8_t len = 0;
        int repeat = 0;
        if (sym == 16) {
            if (index == 0) throw GunzipError("gzip: repeat with no previous length");
            len = lengths[index - 1];
            repeat = 3 + int(in.bits(2));
        } else if (sym == 17) {
            repeat = 3 + int(in.bits(3));
        } else {
            repeat = 11 + int(in.bits(7));
        }
        if (index + repeat > nlen + ndist) throw GunzipError("gzip: too many lengths");
        while (repeat--) lengths[index++] = len;
    }
    if (lengths[256] == 0) throw GunzipError("gzip: no end-of-block code");
    build(lit, lengths, nlen);
    build(dist, lengths + nlen, ndist);
}

uint32_t crc32(const uint8_t* data, size_t n) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) c = table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

}  // namespace

bool isGzip(const uint8_t* data, size_t size) { return size >= 2 && data[0] == 0x1f && data[1] == 0x8b; }

std::vector<uint8_t> gunzip(const uint8_t* data, size_t size, size_t maxOutput) {
    if (size < 18 || !isGzip(data, size) || data[2] != 8) throw GunzipError("gzip: not a deflate gzip member");
    const uint8_t flags = data[3];
    Bits in(data, size - 8);
    in.skip(10);
    if (flags & 4) {  // FEXTRA
        in.skip(2);
        const size_t xlen = size_t(in.at()[-2]) | size_t(in.at()[-1]) << 8;
        in.skip(xlen);
    }
    for (int f : {8, 16}) {  // FNAME, FCOMMENT: zero-terminated
        if (flags & f) {
            while (in.at()[0] != 0) in.skip(1);
            in.skip(1);
        }
    }
    if (flags & 2) in.skip(2);  // FHCRC
    std::vector<uint8_t> out;
    Huffman lit, dist;
    for (;;) {
        const unsigned last = in.bit();
        const unsigned type = in.bits(2);
        if (type == 0) {
            in.alignByte();
            in.skip(4);
            const uint8_t* p = in.at() - 4;
            const unsigned len = unsigned(p[0]) | unsigned(p[1]) << 8;
            const unsigned nlen = unsigned(p[2]) | unsigned(p[3]) << 8;
            if ((len ^ 0xffffu) != nlen) throw GunzipError("gzip: bad stored block");
            if (len > maxOutput - out.size()) throw GunzipError("gzip: output too large");
            const uint8_t* src = in.at();
            in.skip(len);
            out.insert(out.end(), src, src + len);
        } else if (type == 1) {
            fixedTables(lit, dist);
            codes(in, out, lit, dist, maxOutput);
        } else if (type == 2) {
            dynamicTables(in, lit, dist);
            codes(in, out, lit, dist, maxOutput);
        } else {
            throw GunzipError("gzip: bad block type");
        }
        if (last) break;
    }
    const uint8_t* trailer = data + size - 8;
    if (le32(trailer) != crc32(out.data(), out.size())) throw GunzipError("gzip: CRC mismatch");
    if (le32(trailer + 4) != uint32_t(out.size())) throw GunzipError("gzip: length mismatch");
    return out;
}

}  // namespace rp
