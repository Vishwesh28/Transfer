#include <csignal>
#include <iostream>
#include <chrono>
#include <iomanip>
using namespace std;

using Clock = chrono::steady_clock;

constexpr uint64_t JIFFIES_PER_SEC = 1 << 16;            // 65,536 jiffies/sec
constexpr uint64_t START_TIME_SEC = 9 * 3600;
constexpr uint64_t END_TIME_SEC = 15 * 3600 + 30 * 60;
constexpr uint64_t TOTAL_SECONDS = END_TIME_SEC - START_TIME_SEC;
volatile constexpr uint64_t TOTAL_JIFFIES = TOTAL_SECONDS * JIFFIES_PER_SEC;

volatile bool keep_running = true;

chrono::high_resolution_clock::time_point start_time;
uint64_t tick_count = 0;

void handle_sigint(int) {
    keep_running = false;
}

int main() {
    signal(SIGINT, handle_sigint);

    volatile uint64_t factor = 0;
    volatile uint64_t ticks = 0;
    start_time = chrono::high_resolution_clock::now();

    if(factor==0){
        for(;keep_running && ticks < TOTAL_JIFFIES;){
            ticks++;
        }
    }else{
        for(;keep_running && ticks < TOTAL_JIFFIES;){
            ticks++;
            volatile int i = 0;
            while(i<factor){i++;};
        }
    }
    
    auto end_time = chrono::high_resolution_clock::now();
    auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    double seconds = elapsed_ms / 1000.0;
    double sim_seconds = ticks / (double)JIFFIES_PER_SEC;

    cout << "\n--- Tick Simulation Ended ---\n";
    cout << "Total Ticks: " << ticks << "\n";
    cout << "Elapsed Time: " << seconds << " sec\n";
    cout << "Simulated seconds: " << sim_seconds << " s\n";
    cout << "Tick Rate: " << (ticks / seconds) << " ticks/sec\n";
    cout << "Speedup factor observed: " << ((ticks / seconds)/65536) << endl;

    return 0;
}
