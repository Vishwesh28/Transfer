#include <iostream>
#include <chrono>
#include <csignal>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iomanip>
#include <thread>
#include <sys/eventfd.h>

using namespace std;
using Clock = chrono::high_resolution_clock;

constexpr uint64_t JIFFIES_PER_SEC = 1 << 16;

volatile bool keep_running = true;

void handle_sigint(int) {
    keep_running = false;
}

int main() {
    signal(SIGINT, handle_sigint);

    const char* shm_name = "/tick_shm";
    int fd = shm_open(shm_name, O_RDONLY, 0666);
    if (fd < 0) {
        cerr << "Error: Unable to open shared memory.\n";
        return 1;
    }

    auto* tick_ptr = (volatile uint64_t*) mmap(nullptr, sizeof(uint64_t), PROT_READ, MAP_SHARED, fd, 0);
    if (tick_ptr == MAP_FAILED) {
        cerr << "Error: Failed to map shared memory.\n";
        close(fd);
        return 1;
    }

    if (!keep_running) {
        cout << "Terminated before writer started.\n";
        munmap((void*)tick_ptr, sizeof(uint64_t));
        close(fd);
        return 0;
    }

    volatile uint64_t last_seen = 0;
    volatile uint64_t received_ticks = 0;
    volatile uint64_t current = *tick_ptr;

    auto start_time = Clock::now();

    cout << "Waiting for writer to start ticking...\n";

    int efd = ... // open the same eventfd or receive it from parent

    uint64_t received_ticks = 0;
    uint64_t tick_delta;

    while (keep_running) {
        ssize_t n = read(efd, &tick_delta, sizeof(tick_delta));
        if (n != sizeof(tick_delta)) {
            perror("read");
            break;
        }
        received_ticks += tick_delta;
        // Optionally log or do something per batch
    }
    
    // for(;keep_running;){
    //     current = *tick_ptr;
    //     if (current > last_seen){
    //         received_ticks ++;
    //         // cout<<"received tick"<<endl;
    //         last_seen = current;
    //     }
    // }

    auto end_time = Clock::now();
    cout<<"End\n";
    auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    double elapsed_sec = elapsed_ms / 1000.0;
    double sim_seconds = received_ticks / static_cast<double>(JIFFIES_PER_SEC);
    double tick_rate = received_ticks / elapsed_sec;

    cout << fixed << setprecision(6);
    cout << "\n--- Tick Emitter Stats ---\n";
    cout << "Total Ticks Received: " << received_ticks << "\n";
    cout << "Elapsed Time:         " << elapsed_sec << " sec\n";
    cout << "Simulated Seconds:    " << sim_seconds << " s\n";
    // cout << "Tick Rate:            " << tick_rate << " ticks/sec\n";
    // cout << "Speedup Factor:       " << (sim_seconds / elapsed_sec) << "\n";

    munmap((void*)tick_ptr, sizeof(uint64_t));
    close(fd);
    return 0;
}
