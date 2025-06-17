#include <iostream>
#include <chrono>
#include <csignal>
#include <atomic>
using namespace std;

constexpr double jiffy_duration_ns = 1'000'000'000.0 / 65536.0;

atomic<bool> running(true);
chrono::high_resolution_clock::time_point start_time;
uint64_t tick_count = 0;

void handle_sigint(int signal) {
    running = false;
}

int main() {
    // Register Ctrl+C handler
    signal(SIGINT, handle_sigint);

    tick_count = 0;
    start_time = chrono::high_resolution_clock::now();

    while (running) {
        auto tick_start = chrono::high_resolution_clock::now();

        // Busy-wait loop to simulate jiffy timing
        while (true) {
            auto now = chrono::high_resolution_clock::now();
            auto elapsed_ns = chrono::duration_cast<chrono::nanoseconds>(now - tick_start).count();
            if (elapsed_ns >= jiffy_duration_ns) break;
        }

        tick_count++;
    }

    // On Ctrl+C — print stats
    auto end_time = chrono::high_resolution_clock::now();
    auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    double seconds = elapsed_ms / 1000.0;

    cout << "\n--- Tick Simulation Ended ---\n";
    cout << "Total Ticks: " << tick_count << "\n";
    cout << "Elapsed Time: " << seconds << " sec\n";
    cout << "Tick Rate: " << (tick_count / seconds) << " ticks/sec\n";

    return 0;
}
