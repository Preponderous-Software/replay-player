#include <cstring>
#include <vector>

#include "check.h"
#include "model/gunzip.h"
#include "model/json.h"

int main(int argc, char** argv) {
    const std::string data = argc > 1 ? argv[1] : "tests/data";
    // Every chunk of the test bundle gunzips to a UWR1 chunk of the size replay.json lists.
    const auto doc = rp::json::parse(readAll(data + "/scenario/replay.json"));
    int chunks = 0;
    for (const auto& c : doc.find("chunks")->arr) {
        const std::string gz = readAll(data + "/scenario/" + c.find("file")->str);
        const auto* p = reinterpret_cast<const uint8_t*>(gz.data());
        CHECK(rp::isGzip(p, gz.size()));
        const std::vector<uint8_t> raw = rp::gunzip(p, gz.size());
        CHECK(raw.size() == size_t(c.find("raw")->num));
        CHECK(raw.size() >= 4 && std::memcmp(raw.data(), "UWR1", 4) == 0);
        // A flipped byte in the body is caught (by the decoder or the CRC); a truncated file too.
        std::string bad = gz;
        bad[gz.size() / 2] = char(bad[gz.size() / 2] ^ 0x10);
        CHECK_THROWS(rp::gunzip(reinterpret_cast<const uint8_t*>(bad.data()), bad.size()));
        CHECK_THROWS(rp::gunzip(p, gz.size() - 9));
        // An output cap below the real size is refused.
        CHECK_THROWS(rp::gunzip(p, gz.size(), raw.size() - 1));
        ++chunks;
    }
    CHECK(chunks == 4);
    // A stored (uncompressed) block: gzip.compress(b"hello, replay", compresslevel=0, mtime=0).
    const uint8_t stored[] = {0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x01, 0x0d, 0x00, 0xf2,
                              0xff, 'h',  'e',  'l',  'l',  'o',  ',',  ' ',  'r',  'e',  'p',  'l',  'a',  'y',
                              0x09, 0xe1, 0x2b, 0xf0, 0x0d, 0x00, 0x00, 0x00};
    const auto out = rp::gunzip(stored, sizeof stored);
    CHECK(std::string(out.begin(), out.end()) == "hello, replay");
    return finish("gunzip");
}
