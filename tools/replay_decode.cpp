// replay-decode: the bundle decoder on its own, without an engine (tests and tools).
//
//   replay-decode BUNDLE_DIR          every chunk decoded, as canonical JSON, one line per chunk
//                                     (tests/cross_decode.py prints usd-web's decoder the same way)
//   replay-decode --info BUNDLE_DIR   a summary of the model: samples, sets, fields, stats
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "model/bundle.h"

int main(int argc, char** argv) {
    const bool info = argc == 3 && std::strcmp(argv[1], "--info") == 0;
    if (!(argc == 2 || info)) {
        std::fprintf(stderr, "usage: replay-decode [--info] BUNDLE_DIR\n");
        return 2;
    }
    const std::string dir = argv[argc - 1];
    try {
        rp::BundleIndex index;
        const rp::ReplayModel m = rp::loadBundleDir(dir, &index);
        if (info) {
            std::printf("{\"samples\":%d,\"chunks\":%zu,\"sets\":%zu,\"fields\":%zu,\"stats\":%zu,\"scene\":%zu,\"firstTick\":%lld,"
                        "\"lastTick\":%lld,\"lastHash\":\"%s\"}\n",
                        m.sampleCount(), index.chunks.size(), m.sets.size(), m.fields.size(), m.stats.size(), m.scene.size(),
                        (long long)m.ticks.front(), (long long)m.ticks.back(), m.hashes.back().c_str());
            return 0;
        }
        for (const rp::ChunkInfo& c : index.chunks) {
            std::ifstream f(dir + "/" + c.file, std::ios::binary);
            const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            const std::string line = rp::decodedToJson(rp::decodeChunk(bytes.data(), bytes.size(), m, index), m, index);
            std::fwrite(line.data(), 1, line.size(), stdout);
            std::fputc('\n', stdout);
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "replay-decode: %s\n", e.what());
        return 1;
    }
}
