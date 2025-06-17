#include <csignal>
#include <iostream>
using namespace std;

volatile bool keep_running = true;

chrono::high_resolution_clock::time_point start_time;
uint64_t tick_count = 0;

void handle_sigint(int) {
    keep_running = false;
}

int main() {
    signal(SIGINT, handle_sigint);

    uint64_t ticks = 0;
    start_time = chrono::high_resolution_clock::now();

    asm volatile (
        "mov %[ticks], %%rax\n\t"    // Move ticks to RAX
        "1:\n\t"
        "inc %%rax\n\t"              // Increment RAX
        "cmpb $0, %[keep_running]\n\t"
        "jne 1b\n\t"
        "mov %%rax, %[ticks]\n\t"    // Store RAX back to ticks
        : [ticks] "+r" (ticks)
        : [keep_running] "m" (keep_running)
        : "rax"
    );

    auto end_time = chrono::high_resolution_clock::now();
    auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    double seconds = elapsed_ms / 1000.0;

    cout << "\n--- Tick Simulation Ended ---\n";
    cout << "Total Ticks: " << ticks << "\n";
    cout << "Elapsed Time: " << seconds << " sec\n";
    cout << "Tick Rate: " << (ticks / seconds) << " ticks/sec\n";
    cout << "How many times quicker than a jifyy: " << ((ticks / seconds)/65536) << endl;
    return 0;
}
