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
constexpr size_t RING_SIZE = 3*JIFFIES_PER_SEC;
constexpr size_t RING_MASK = RING_SIZE - 1;

volatile bool keep_running = true;
uint64_t events_processed = 0;

struct DateConfig {
    char start_date[12];  // "YYYY-MM-DD\0"
    char end_date[12];    // "YYYY-MM-DD\0"
};

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

// Date structure for easier handling
struct Date {
    int year, month, day;
    
    Date(int y, int m, int d) : year(y), month(m), day(d) {}
    
    // Convert to days since epoch (1970-01-01)
    int toDaysSinceEpoch() const {
        struct tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day;
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;
        
        time_t time = mktime(&tm);
        return time / (24 * 3600);
    }
    
    // Add days to current date
    Date addDays(int days) const {
        struct tm tm = {};
        tm.tm_year = year - 1900;
        tm.tm_mon = month - 1;
        tm.tm_mday = day + days;
        tm.tm_hour = 0;
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_isdst = -1;
        
        time_t time = mktime(&tm);
        struct tm* result = localtime(&time);
        
        return Date(result->tm_year + 1900, result->tm_mon + 1, result->tm_mday);
    }
    
    bool operator<=(const Date& other) const {
        return toDaysSinceEpoch() <= other.toDaysSinceEpoch();
    }
    
    string toString() const {
        ostringstream oss;
        oss << year << "-" << setfill('0') << setw(2) << month << "-" << setw(2) << day;
        return oss.str();
    }
};

// Parse date from string (YYYY-MM-DD format)
Date parseDate(const string& dateStr) {
    int year, month, day;
    char dash1, dash2;
    istringstream iss(dateStr);
    
    if (!(iss >> year >> dash1 >> month >> dash2 >> day) || dash1 != '-' || dash2 != '-') {
        throw invalid_argument("Invalid date format. Use YYYY-MM-DD");
    }
    
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        throw invalid_argument("Invalid date values");
    }
    
    return Date(year, month, day);
}

// Get number of jiffies from Jan 1, 1980 to virtual day 9:00 AM
uint64_t jiffies_from_1980_to_virtual_day(const Date& target_date) {
    // Epoch: 1980-01-01 00:00:00
    std::tm base_tm = {};
    base_tm.tm_year = 1980 - 1900;
    base_tm.tm_mon = 0;
    base_tm.tm_mday = 1;
    base_tm.tm_hour = 0;
    base_tm.tm_min = 0;
    base_tm.tm_sec = 0;
    base_tm.tm_isdst = -1;
    time_t base_time = mktime(&base_tm);

    // Virtual 9:00 AM of target date
    std::tm day_tm = {};
    day_tm.tm_year = target_date.year - 1900;
    day_tm.tm_mon = target_date.month - 1;
    day_tm.tm_mday = target_date.day;
    day_tm.tm_hour = 9;
    day_tm.tm_min = 0;
    day_tm.tm_sec = 0;
    day_tm.tm_isdst = -1;
    time_t day_time = mktime(&day_tm);

    return static_cast<uint64_t>(day_time - base_time) * JIFFIES_PER_SEC;
}

// Read date configuration from separate shared memory
bool readDateConfig(Date& start_date, Date& end_date) {
    const char* shm_config_name = "/date_config";
    
    int config_fd = shm_open(shm_config_name, O_RDONLY, 0666);
    if (config_fd < 0) {
        cerr << "Error: Unable to open date config shared memory.\n";
        return false;
    }
    
    size_t config_size = sizeof(DateConfig);
    auto* date_config = static_cast<DateConfig*>(
        mmap(nullptr, config_size, PROT_READ, MAP_SHARED, config_fd, 0)
    );
    
    if (date_config == MAP_FAILED) {
        cerr << "Error: Failed to map date config shared memory.\n";
        close(config_fd);
        return false;
    }
    
    cout << "Date configuration received:\n";
    cout << "  Start Date: " << date_config->start_date << "\n";
    cout << "  End Date: " << date_config->end_date << "\n";
    
    try {
        start_date = parseDate(string(date_config->start_date));
        end_date = parseDate(string(date_config->end_date));
    } catch (const exception& e) {
        cerr << "Error parsing dates from config: " << e.what() << endl;
        munmap(date_config, config_size);
        close(config_fd);
        return false;
    }
    
    munmap(date_config, config_size);
    close(config_fd);
    return true;
}

void sleep_until_next_9am() {
    cout << "[INFO] Sleeping until next market day...\n";
    this_thread::sleep_for(chrono::seconds(80)); 
}

void handle_sigint(int) {
    keep_running = false;
}

// Your custom tick processing function
inline void process_tick_event() {
    ++events_processed;
    // ticks + evenevents_processed is my current jiffy
}

int main() {
    signal(SIGINT, handle_sigint);

    Date start_date(2024, 9, 2);  // Default values
    Date end_date(2024, 9, 3);

    if (!readDateConfig(start_date, end_date)) {
        cerr << "Failed to read date configuration. Exiting.\n";
        return 1;
    }

    cout << "=== RECEIVER STARTING ===\n";
    cout << "Will process dates from " << start_date.toString() 
         << " to " << end_date.toString() << "\n";

    Date current_date = start_date;
    int total_days = 0;

    const char* shm_name = "/simple_ring_buffer1";
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

    while (current_date <= end_date && keep_running) {

        cout << "\n=== WAITING FOR DATE: " << current_date.toString() << " ===\n";

        uint64_t ticks = jiffies_from_1980_to_virtual_day(current_date);
        cout << "Jiffies before today start: " << ticks << endl;

        cout << "Generator started for " << current_date.toString() << "! Beginning event processing...\n";
    
        auto start_time = chrono::high_resolution_clock::now();
        
        // Simple ring buffer consumer logic
        while(keep_running) {
            bool processed_events = false;
            
            // Process available events
            while(ring->tail != ring->head) {
                // size_t index = ring->tail % RING_SIZE;
                // TickEvent event = ring->events[index];
                
                process_tick_event();
                
                ring->tail = (ring->tail + 1) % RING_SIZE;
                processed_events = true;
            }
            
            // Check if producer finished and buffer is empty
            if (!processed_events && ring->producer_finished) {
                // Final drain
                while(ring->tail != ring->head) {
                    // size_t index = ring->tail % RING_SIZE;
                    // TickEvent event = ring->events[index];
                    
                    process_tick_event();
                    
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

            // if (ring->producer_finished) {
            //         cout << "All events processed. Shutting down.\n";
            //         break;
            //     }
        }

        auto end_time = chrono::high_resolution_clock::now();
        
        auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
        double elapsed_sec = elapsed_ms / 1000.0;
        double sim_seconds = events_processed / static_cast<double>(JIFFIES_PER_SEC);
        double processing_rate = events_processed / elapsed_sec;

        cout << fixed << setprecision(6);
        cout << "\n=== SIMPLE RING BUFFER RECEIVER STATS ===\n";
        // cout << "Events Processed:         " << events_processed << "\n";
        cout << "Total Generated:          " << ring->total_generated << "\n";
        cout << "Dropped by Producer:      " << ring->dropped_count << "\n";
        cout << "Processing Success Rate:  " << (100.0 * events_processed / ring->total_generated) << "%\n";
        cout << "Simulated Time:           " << sim_seconds << " sec\n";

        total_days++;

        events_processed = 0;
        
        // Move to next day
        current_date = current_date.addDays(1);
        
        // Sleep until next day (if not the last day)
        if (current_date <= end_date && keep_running) {
            sleep_until_next_9am(); 
        }

    }

    munmap(ring, shm_size);
    close(fd);
    return 0;
}
