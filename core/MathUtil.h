#pragma once
#include "Log.h"
#include <cmath>

namespace rdm {
    constexpr bool isPrime(unsigned long n) {
        if (n <= 1) return false;
        if (n <= 19) {
            return (n==2 || n==3 || n==5 || n==7 || n==11 || n==13 || n==17 || n==19);
        }
        if (n%2==0 || n%3==0 || n%5==0 || n%7==0 || n%11==0 || n%13==0 || n%17==0 || n%19==0)
            return false;
        for (unsigned long d = 23; d*d <= n; d += 2) {
            if (n % d == 0) return false;
        }
        return true;
    }

    inline unsigned long prevPrime(unsigned long n) {
        if (n <= 2) return 2;
        if (n % 2 == 0) n--;
        while (n > 0) {
            if (isPrime(n)) return n;
            n -= 2;
        }
        return 0;
    }

    inline int log4(size_t n) {
        log_assert(n > 0);
        return static_cast<int>(std::log(n + 0.5) / std::log(4.0));
    }
}
