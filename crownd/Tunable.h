#pragma once

#include <vector>
#include <string>
#include <algorithm>

namespace tune {

    struct Params {
        const char* name;
        int* ref;
        int def;
        int min;
        int max;
        double cEnd;
    };

    inline std::vector<Params> params;

    inline int add(const char* name, int* ref, int def, int min, int max, double cEnd) {
        params.push_back({ name, ref, def, min, max, cEnd });
        return def;
    }

    inline bool set(const std::string& name, int value) {
        for (auto& p : params) {
            if (name == p.name) {
                *p.ref = std::clamp(value, p.min, p.max);
                return true;
            }
        }
        return false;
    }
}

#ifdef TUNE
#define TUNABLE(name, def, min, max, cEnd)                    \
        inline int name = tune::add(#name, &name, def, min, max, cEnd)
#else
#define TUNABLE(name, def, min, max, cEnd)                    \
        inline constexpr int name = def
#endif