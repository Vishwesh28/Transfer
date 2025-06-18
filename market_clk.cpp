#include <iostream>
#include <chrono>
#include <csignal>
#include <iomanip>

using namespace std;
using Clock = chrono::steady_clock;

// Constants
constexpr uint64_t JIFFIES_PER_SEC = 1 << 16;            // 65,536 jiffies/sec
constexpr uint64_t START_TIME_SEC = 9 * 3600;
constexpr uint64_t END_TIME_SEC = 15 * 3600 + 30 * 60;
constexpr uint64_t TOTAL_SECONDS = END_TIME_SEC - START_TIME_SEC;
constexpr uint64_t TOTAL_JIFFIES = TOTAL_SECONDS * JIFFIES_PER_SEC;

// Volatile state

volatile bool keep_running = true;

// Ctrl+C handler
void handle_sigint(int) {
    keep_running = false;
    cout << "\n[Ctrl+C received — stopping simulation...]\n";
}

// Print simulated HH:MM:SS.jiffy
void print_jiffy_time(uint64_t jiffy) {
    uint64_t total_sec = jiffy / JIFFIES_PER_SEC + START_TIME_SEC;
    uint64_t jiff_in_sec = jiffy % JIFFIES_PER_SEC;
    uint64_t hr = total_sec / 3600;
    uint64_t min = (total_sec % 3600) / 60;
    uint64_t sec = total_sec % 60;

    cout << setfill('0') << setw(2) << hr << ":"
         << setw(2) << min << ":"
         << setw(2) << sec << "."
         << setw(5) << jiff_in_sec << "\r" << flush;
}

int main() {
    int factor = 1000; // Speed multiplier (1 = real time)

    // Duration of one jiffy at current factor
    double jiffy_duration_ns = 1e9 / JIFFIES_PER_SEC / factor;

    // Register SIGINT (Ctrl+C) handler
    signal(SIGINT, handle_sigint);

    // Clock start
    auto sim_start = Clock::now();
    volatile uint64_t jiffy_tick = 0;

    while (keep_running && jiffy_tick < TOTAL_JIFFIES) {
        // Target time for this tick
        auto target_time = sim_start + chrono::nanoseconds((uint64_t)(jiffy_tick * jiffy_duration_ns));

        // // Busy-wait until that time is reached
        while (Clock::now() < target_time);

        jiffy_tick++;

        // if (jiffy_tick % JIFFIES_PER_SEC == 0) {
        //     print_jiffy_time(jiffy_tick);
        // }
    }

    // Final time
    auto sim_end = Clock::now();
    chrono::duration<double> real_elapsed = sim_end - sim_start;
    double sim_seconds = jiffy_tick / (double)JIFFIES_PER_SEC;

    cout << "\n\n--- Final Simulation Stats ---\n";
    cout << "Simulated seconds: " << fixed << setprecision(6) << sim_seconds << " s\n";
    cout << "Wall-clock seconds: " << fixed << setprecision(6) << real_elapsed.count() << " s\n";
    cout << "Speedup factor observed: " << sim_seconds / real_elapsed.count() << "x\n";

    return 0;
}
