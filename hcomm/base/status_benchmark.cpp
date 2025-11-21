// SPDX-License-Identifier: MulanPSL-2.0

#include "hcomm/base/status.hpp"

#include <benchmark/benchmark.h>

namespace {
void BM_CreateOk(benchmark::State& state) {
    for (auto _ : state) {
        hcomm::Status ok(hcomm::StatusCode::Ok);
        benchmark::DoNotOptimize(ok);
    }
}
BENCHMARK(BM_CreateOk);

void BM_CreateBad(benchmark::State& state) {
    for (auto _ : state) {
        hcomm::Status bad(hcomm::StatusCode::InvalidArgument, "invalid argument");
        benchmark::DoNotOptimize(bad);
    }
}
BENCHMARK(BM_CreateBad);
} // namespace
