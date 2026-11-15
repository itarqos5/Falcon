#pragma once

// A sampling profiler for finding what the time goes to, for PCs that have none (this one does not): every quarter of a millisecond the instruction pointer of each
// registered thread is read, and what the samples hit is written out when it is done. Only on when FALCON_SAMPLE=<file> is in the environment, and then only for Windows
// (SuspendThread and GetThreadContext); elsewhere, and when off, everything here does nothing.
//
//   FALCON_SAMPLE=out.txt Falcon.exe ... --bench 20     samples the main thread, the worker threads and the other registered threads during the benchmark
//   node tools/resolve_samples.mjs out.txt Falcon.exe   the functions they were in (the program's own by name, other modules as [module.dll])
//
// Threads register themselves (registerThread) when they start; sampling begins at start() and ends at finish(), which writes the file.
namespace falcon::profiler {

// True when FALCON_SAMPLE is set (read once).
[[nodiscard]] bool enabled();
// Adds the calling thread to the threads that are sampled, under a name that the samples carry ("main", "worker"). Cheap when not enabled.
void registerThread(const char* name = "thread");
// Starts and stops sampling. finish() writes the samples to the file FALCON_SAMPLE names, and prints how many.
void start();
void finish();

}  // namespace falcon::profiler
