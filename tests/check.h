// A tiny test harness: CHECK records a failure and goes on; main returns the failure count.
#pragma once

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

inline int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                           \
        }                                                                           \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(std::fabs(double(a) - double(b)) <= (eps))

#define CHECK_THROWS(expr)                  \
    do {                                    \
        bool threw_ = false;                \
        try {                               \
            (void)(expr);                   \
        } catch (const std::exception&) {   \
            threw_ = true;                  \
        }                                   \
        CHECK(threw_ && #expr " throws");   \
    } while (0)

inline std::string readAll(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

inline int finish(const char* name) {
    if (g_failures) std::fprintf(stderr, "%s: %d failure(s)\n", name, g_failures);
    else std::printf("%s: ok\n", name);
    return g_failures ? 1 : 0;
}
