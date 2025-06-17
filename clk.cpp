#include <iostream>
#include <chrono>
#include <cstdint>
using namespace std;

constexpr double jiffy_duration_ns = 1'000'000'000.0 / 65536.0; // ≈ 15258.789 ns

int main() {
    uint64_t tick_count = 0;
    auto global_start = chrono::high_resolution_clock::now();

    while (true) {
        auto tick_start = chrono::high_resolution_clock::now();

        // Busy-wait until jiffy duration passes (in nanoseconds)
        while (true) {
            auto now = chrono::high_resolution_clock::now();
            auto elapsed_ns = chrono::duration_cast<chrono::nanoseconds>(now - tick_start).count();
            if (elapsed_ns >= jiffy_duration_ns) break;
        }

        tick_count++;

        // Print stats every second (i.e., 65536 jiffies)
        if (tick_count % 65536 == 0) {
            auto now = chrono::high_resolution_clock::now();
            auto total_elapsed = chrono::duration_cast<chrono::milliseconds>(now - global_start).count();
            double seconds = total_elapsed / 1000.0;

            cout << "Ticks: " << tick_count
                      << " | Elapsed: " << seconds << " sec"
                      << " | Rate: " << (tick_count / seconds) << " ticks/sec\n";
        }
    }

    return 0;
}
