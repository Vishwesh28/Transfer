#include <csignal>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/eventfd.h>

using namespace std;
using Clock = chrono::steady_clock;

constexpr uint64_t JIFFIES_PER_SEC = 1 << 16;            // 65,536 jiffies/sec
constexpr uint64_t START_TIME_SEC = 9 * 3600;
constexpr uint64_t END_TIME_SEC = 15 * 3600 + 30 * 60;
constexpr uint64_t TOTAL_SECONDS = END_TIME_SEC - START_TIME_SEC;
volatile constexpr uint64_t TOTAL_JIFFIES = TOTAL_SECONDS * JIFFIES_PER_SEC;

volatile bool keep_running = true;

void handle_sigint(int) {
    keep_running = false;
}

int main() {
    signal(SIGINT, handle_sigint);

    // Shared memory setup
    const char* shm_name = "/tick_shm";
    int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    ftruncate(fd, sizeof(uint64_t));
    auto* tick_ptr = (volatile uint64_t*) mmap(nullptr, sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    *tick_ptr = 0;
    cout << "Writer to start ticking...\n";
    volatile uint64_t factor = 100;
    volatile uint64_t& ticks = *tick_ptr;

    // sleep(10);

    // Start clock
    cout<<"Start\n";
    

    int efd = eventfd(0, 0);  // you can pass flags like EFD_NONBLOCK
    if (efd < 0) {
        perror("eventfd");
        return 1;
    }

    auto start_time = chrono::high_resolution_clock::now();

    for (;keep_running && ticks < TOTAL_JIFFIES;) {
        uint64_t one = 1;
        write(efd, &one, sizeof(one));  // sends one tick
        ticks++;
        if (factor > 0) {
            volatile int i = 0;
            while (i < factor) { i++; }
        }
    }

    auto end_time = chrono::high_resolution_clock::now();
    volatile uint64_t x = ticks;
    ticks=1000000000;
    cout<<x<<" "<<ticks<<endl;
    auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    double seconds = elapsed_ms / 1000.0;
    double sim_seconds = x / 65536.0;

    cout << fixed << setprecision(6);
    cout << "\n--- Tick Simulation Ended ---\n";
    cout << "Total Ticks:        " << x << "\n";
    cout << "Elapsed Time:       " << seconds << " sec\n";
    cout << "Simulated Seconds:  " << sim_seconds << " s\n";
    cout << "Tick Rate:          " << (x / seconds) << " ticks/sec\n";
    cout << "Speedup Factor:     " << (sim_seconds / seconds) << "\n";

    // Cleanup
    munmap((void*)tick_ptr, sizeof(uint64_t));
    close(fd);
    shm_unlink(shm_name);

    return 0;
}