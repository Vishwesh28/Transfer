#include <csignal>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <thread>
#include <cstring>

using namespace std;

constexpr uint64_t JIFFIES_PER_SEC = 1 << 16;
constexpr uint64_t START_TIME_SEC = 9 * 3600;
constexpr uint64_t END_TIME_SEC = 15 * 3600 + 30 * 60;
constexpr uint64_t TOTAL_SECONDS = END_TIME_SEC - START_TIME_SEC;
constexpr uint64_t TOTAL_JIFFIES = TOTAL_SECONDS * JIFFIES_PER_SEC;

// Ring buffer size - must be power of 2
constexpr size_t RING_SIZE = 2*JIFFIES_PER_SEC;  // 64K entries
constexpr size_t RING_MASK = RING_SIZE - 1;

// Simple tick event
struct TickEvent {
    uint64_t tick_number;
    uint64_t timestamp_ns;
};

// Simplified shared structure
struct SharedRingBuffer {
    // Control flags
    volatile bool producer_running;
    volatile bool producer_finished;
    volatile uint64_t total_generated;
    volatile uint64_t dropped_count;
    
    // Ring buffer indices - use regular volatiles to avoid alignment issues
    volatile uint64_t head;
    volatile uint64_t tail;
    
    // Padding to separate from data
    char padding[64];
    
    // Ring buffer data
    TickEvent events[RING_SIZE];
};

volatile bool keep_running = true;

void handle_sigint(int) {
    keep_running = false;
}

int main() {
    signal(SIGINT, handle_sigint);

    const char* shm_name1 = "/simple_ring_buffer1";
    const char* shm_name2 = "/simple_ring_buffer2";

    int fd1 = shm_open(shm_name1, O_CREAT | O_RDWR, 0666);
    if (fd1 < 0) {
        perror("shm_open1 failed");
        return 1;
    }
    int fd2 = shm_open(shm_name2, O_CREAT | O_RDWR, 0666);
    if (fd2 < 0) {
        perror("shm_open2 failed");
        return 1;
    }
    
    size_t shm_size = sizeof(SharedRingBuffer);
    if (ftruncate(fd1, shm_size) < 0) {
        perror("ftruncate failed");
        close(fd1);
        shm_unlink(shm_name1);
        return 1;
    }
    if (ftruncate(fd2, shm_size) < 0) {
        perror("ftruncate failed");
        close(fd2);
        shm_unlink(shm_name2);
        return 1;
    }
    
    auto* ring1 = static_cast<SharedRingBuffer*>(
        mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd1, 0)
    );
    auto* ring2 = static_cast<SharedRingBuffer*>(
        mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0)
    );
    
    if (ring1 == MAP_FAILED) {
        perror("mmap1 failed");
        close(fd1);
        shm_unlink(shm_name1);
        return 1;
    }
    if (ring2 == MAP_FAILED) {
        perror("mmap2 failed");
        close(fd2);
        shm_unlink(shm_name2);
        return 1;
    }

    // Initialize shared memory
    memset(ring1, 0, sizeof(SharedRingBuffer));
    memset(ring2, 0, sizeof(SharedRingBuffer));
    
    cout << "Simple Ring Buffer Generator ready. Buffer size: " << RING_SIZE << " events\n";
    cout << "Shared memory size: " << shm_size << " bytes\n";
    cout << "Waiting 10 seconds for receiver...\n";
    sleep(10);

    volatile uint64_t factor = 0;
    uint64_t tick_count = 0;
    uint64_t successful_writes = 0;
    uint64_t dropped = 0;
    uint64_t bufferfull = 0;
    
    cout << "Starting tick generation...\n";
    ring1->producer_running = true;
    ring2->producer_running = true;
    
    auto start_time = chrono::high_resolution_clock::now();

    // Simple ring buffer logic
    if(factor == 0) {
        // Maximum speed
        while(keep_running && tick_count < TOTAL_JIFFIES) {
            bool buffer_1_has_space = (ring1->head + 1) % RING_SIZE != ring1->tail;
            bool buffer_2_has_space = (ring2->head + 1) % RING_SIZE != ring2->tail;

            if (buffer_1_has_space && buffer_2_has_space) {
                // Get timestamp once
                auto now = chrono::high_resolution_clock::now();
                uint64_t timestamp = chrono::duration_cast<chrono::nanoseconds>(
                    now.time_since_epoch()).count();
                
                // Write to buffer A
                size_t index_1 = ring1->head % RING_SIZE;
                ring1->events[index_1].tick_number = tick_count;
                ring1->events[index_1].timestamp_ns = timestamp;
                ring1->head = (ring1->head + 1) % RING_SIZE;
                
                // Write to buffer B  
                size_t index_2 = ring2->head % RING_SIZE;
                ring2->events[index_2].tick_number = tick_count;
                ring2->events[index_2].timestamp_ns = timestamp;
                ring2->head = (ring2->head + 1) % RING_SIZE;
                
                successful_writes++;
            } else {
                // One or both buffers full
                dropped++;
            }

            tick_count++;
        }
    } else {
        // Throttled generation
        while(keep_running && tick_count < TOTAL_JIFFIES) {
            bool buffer_1_has_space = (ring1->head + 1) % RING_SIZE != ring1->tail;
            bool buffer_2_has_space = (ring2->head + 1) % RING_SIZE != ring2->tail;

            if (buffer_1_has_space && buffer_2_has_space) {
                // Get timestamp once
                auto now = chrono::high_resolution_clock::now();
                uint64_t timestamp = chrono::duration_cast<chrono::nanoseconds>(
                    now.time_since_epoch()).count();
                
                // Write to buffer A
                size_t index_1 = ring1->head % RING_SIZE;
                ring1->events[index_1].tick_number = tick_count;
                ring1->events[index_1].timestamp_ns = timestamp;
                ring1->head = (ring1->head + 1) % RING_SIZE;
                
                // Write to buffer B  
                size_t index_2 = ring2->head % RING_SIZE;
                ring2->events[index_2].tick_number = tick_count;
                ring2->events[index_2].timestamp_ns = timestamp;
                ring2->head = (ring2->head + 1) % RING_SIZE;
                
                successful_writes++;
            } else {
                // One or both buffers full
                dropped++;
            }

            tick_count++;

            // Throttling delay
            volatile uint64_t i = 0;
            while(i < factor) { ++i; }
        }
    }

    auto end_time = chrono::high_resolution_clock::now();
    
    // Update final statistics
    ring1->total_generated = tick_count;
    ring1->dropped_count = dropped;
    ring1->producer_finished = true;

    ring2->total_generated = tick_count;
    ring2->dropped_count = dropped;
    ring2->producer_finished = true;
    
    
    auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    double seconds = elapsed_ms / 1000.0;
    double sim_seconds = tick_count / static_cast<double>(JIFFIES_PER_SEC);

    cout << fixed << setprecision(6);
    cout << "\n=== SIMPLE RING BUFFER GENERATOR STATS ===\n";
    cout << "Total Ticks Generated:    " << tick_count << "\n";
    cout << "Successful Buffer Writes: " << successful_writes << "\n";
    cout << "Dropped Events:           " << dropped << "\n";
    cout << "Drop Rate:                " << (100.0 * dropped / tick_count) << "%\n";
    cout << "Elapsed Time:             " << seconds << " sec\n";
    cout << "Simulated Time:           " << sim_seconds << " sec\n";
    cout << "Generation Rate:          " << (tick_count / seconds) << " ticks/sec\n";
    cout << "Buffer Write Rate:        " << (successful_writes / seconds) << " events/sec\n";
    cout << "Time Speedup Factor:      " << (sim_seconds / seconds) << "x\n";

    

    // Cleanup
    munmap(ring1, shm_size);
    close(fd1);
    shm_unlink(shm_name1);

    munmap(ring2, shm_size);
    close(fd2);
    shm_unlink(shm_name2);

    return 0;
}