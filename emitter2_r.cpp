#include <iostream>
#include <chrono>
#include <csignal>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <iomanip>
#include <thread>

using namespace std;

constexpr uint64_t JIFFIES_PER_SEC = 1 << 16;
constexpr size_t RING_SIZE = 2*JIFFIES_PER_SEC;
constexpr size_t RING_MASK = RING_SIZE - 1;

// Same structures as generator
struct TickEvent {
    uint64_t tick_number;
    uint64_t timestamp_ns;
};

struct SharedRingBuffer {
    volatile bool producer_running;
    volatile bool producer_finished;
    volatile uint64_t total_generated;
    volatile uint64_t dropped_count;
    
    volatile uint64_t head;
    volatile uint64_t tail;
    
    char padding[64];
    
    TickEvent events[RING_SIZE];
};

volatile bool keep_running = true;
uint64_t events_processed = 0;

void handle_sigint(int) {
    keep_running = false;
}

// Your custom tick processing function
inline void process_tick_event(const TickEvent& event) {
    ++events_processed;
}

int main() {
    signal(SIGINT, handle_sigint);

    const char* shm_name = "/simple_ring_buffer2";
    int fd = shm_open(shm_name, O_RDWR, 0666);
    if (fd < 0) {
        cerr << "Error: Unable to open shared memory. Make sure generator is running first.\n";
        return 1;
    }

    size_t shm_size = sizeof(SharedRingBuffer);
    auto* ring = static_cast<SharedRingBuffer*>(
        mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)
    );
    
    if (ring == MAP_FAILED) {
        cerr << "Error: Failed to map shared memory.\n";
        close(fd);
        return 1;
    }

    cout << "Simple Ring Buffer Receiver connected. Buffer size: " << RING_SIZE << " events\n";
    cout << "Waiting for generator to start...\n";
    
    // Wait for generator to start
    // while(keep_running && !ring->producer_running) {
    //     this_thread::sleep_for(chrono::milliseconds(100));
    // }
    
    if (!keep_running) {
        cout << "Terminated before generator started.\n";
        munmap(ring, shm_size);
        close(fd);
        return 0;
    }

    cout << "Generator started! Beginning event processing...\n";
    
    auto start_time = chrono::high_resolution_clock::now();
    
    // Simple ring buffer consumer logic
    while(keep_running) {
        bool processed_events = false;
        
        // Process available events
        while(ring->tail != ring->head) {
            size_t index = ring->tail % RING_SIZE;
            TickEvent event = ring->events[index];
            
            process_tick_event(event);
            
            ring->tail = (ring->tail + 1) % RING_SIZE;
            processed_events = true;
        }
        
        // Check if producer finished and buffer is empty
        if (!processed_events && ring->producer_finished) {
            // Final drain
            while(ring->tail != ring->head) {
                size_t index = ring->tail % RING_SIZE;
                TickEvent event = ring->events[index];
                
                process_tick_event(event);
                
                ring->tail = (ring->tail + 1) % RING_SIZE;
                processed_events = true;
            }
            
            if (!processed_events) {
                cout << "All events processed. Shutting down.\n";
                break;
            }
        }
        
        // Small yield if no events processed
        if (!processed_events) {
            std::this_thread::yield();
        }

        if (ring->producer_finished) {
                cout << "All events processed. Shutting down.\n";
                break;
            }
    }

    auto end_time = chrono::high_resolution_clock::now();
    
    auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
    double elapsed_sec = elapsed_ms / 1000.0;
    double sim_seconds = events_processed / static_cast<double>(JIFFIES_PER_SEC);
    double processing_rate = events_processed / elapsed_sec;

    cout << fixed << setprecision(6);
    cout << "\n=== SIMPLE RING BUFFER RECEIVER STATS ===\n";
    cout << "Events Processed:         " << events_processed << "\n";
    cout << "Total Generated:          " << ring->total_generated << "\n";
    cout << "Dropped by Producer:      " << ring->dropped_count << "\n";
    cout << "Processing Success Rate:  " << (100.0 * events_processed / ring->total_generated) << "%\n";
    cout << "Elapsed Time:             " << elapsed_sec << " sec\n";
    cout << "Simulated Time:           " << sim_seconds << " sec\n";
    cout << "Processing Rate:          " << processing_rate << " events/sec\n";
    cout << "Time Speedup Factor:      " << (sim_seconds / elapsed_sec) << "x\n";

    munmap(ring, shm_size);
    close(fd);
    return 0;
}