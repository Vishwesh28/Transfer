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
#include <sstream>

using namespace std;

constexpr uint64_t JIFFIES_PER_SEC = 1 << 16;
constexpr uint64_t START_TIME_SEC = 9 * 3600;
constexpr uint64_t END_TIME_SEC = 15 * 3600 + 30 * 60;
constexpr uint64_t TOTAL_SECONDS = END_TIME_SEC - START_TIME_SEC;
constexpr uint64_t TOTAL_JIFFIES = TOTAL_SECONDS * JIFFIES_PER_SEC;

// Ring buffer size - must be power of 2
constexpr size_t RING_SIZE = 3*JIFFIES_PER_SEC;  // 64K entries
constexpr size_t RING_MASK = RING_SIZE - 1;

volatile bool keep_running = true;

struct DateConfig {
    char start_date[12];  // "YYYY-MM-DD\0"
    char end_date[12];    // "YYYY-MM-DD\0"
};

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

// Get number of jiffies from Jan 1, 1980 to virtual today 9:00 AM
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


void sleep_until_next_9am() {
    cout << "[INFO] Sleeping until next market day...\n";
    this_thread::sleep_for(chrono::seconds(100)); 
}

void printUsage(const char* program_name) {
    cout << "Usage: " << program_name << " <start_date> <end_date>\n";
    cout << "Date format: YYYY-MM-DD\n";
    cout << "Example: " << program_name << " 2024-09-02 2024-09-30\n";
}

void handle_sigint(int) {
    keep_running = false;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, handle_sigint);

    // Parse command line arguments
    if (argc != 3) {
        printUsage(argv[0]);
        return 1;
    }

    Date start_date(2024, 9, 2);  // Default values
    Date end_date(2024, 9, 3);

    try {
        start_date = parseDate(argv[1]);
        end_date = parseDate(argv[2]);
    } catch (const exception& e) {
        cerr << "Error parsing dates: " << e.what() << endl;
        printUsage(argv[0]);
        return 1;
    }

    if (!(start_date <= end_date)) {
        cerr << "Error: Start date must be before or equal to end date\n";
        return 1;
    }

    cout << "Starting tick generation from " << start_date.toString() 
         << " to " << end_date.toString() << endl;

    Date current_date = start_date;
    int total_days = 0;

    const char* shm_name1 = "/simple_ring_buffer1";
    const char* shm_name2 = "/simple_ring_buffer2";
    const char* shm_config_name = "/date_config";

    // Create date config shared memory
    int config_fd = shm_open(shm_config_name, O_CREAT | O_RDWR, 0666);
    if (config_fd < 0) {
        perror("shm_open config failed");
        return 1;
    }
    
    size_t config_size = sizeof(DateConfig);
    if (ftruncate(config_fd, config_size) < 0) {
        perror("ftruncate config failed");
        close(config_fd);
        shm_unlink(shm_config_name);
        return 1;
    }
    
    auto* date_config = static_cast<DateConfig*>(
        mmap(nullptr, config_size, PROT_READ | PROT_WRITE, MAP_SHARED, config_fd, 0)
    );
    
    if (date_config == MAP_FAILED) {
        perror("mmap config failed");
        close(config_fd);
        shm_unlink(shm_config_name);
        return 1;
    }

    // Initialize and send date config
    memset(date_config, 0, sizeof(DateConfig));
    strcpy(date_config->start_date, start_date.toString().c_str());
    strcpy(date_config->end_date, end_date.toString().c_str());
    
    cout << "Date config sent to emitters via separate shared memory:\n";
    cout << "  Start: " << date_config->start_date << "\n";
    cout << "  End: " << date_config->end_date << "\n";

    // Ring buffers
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

    volatile uint64_t factor = 10;
    uint64_t tick_count = 0;
    uint64_t successful_writes = 0;
    uint64_t dropped = 0;
    uint64_t bufferfull = 0;

    while(current_date <= end_date && keep_running){

        // Reset buffers for new day
        ring1->head = 0;
        ring1->tail = 0;
        ring1->total_generated = 0;
        ring1->dropped_count = 0;
        ring1->producer_running = false;
        ring1->producer_finished = false;
        
        ring2->head = 0;
        ring2->tail = 0;
        ring2->total_generated = 0;
        ring2->dropped_count = 0;
        ring2->producer_running = false;
        ring2->producer_finished = false;

        tick_count = 0;
        successful_writes = 0;
        dropped = 0;

        uint64_t ticks = jiffies_from_1980_to_virtual_day(current_date);
        
        cout << "Jiffies before today start: " << ticks << endl;
        cout << "Starting tick generation for " << current_date.toString() << "...\n";

        ring1->producer_running = true;
        ring2->producer_running = true;
        
        auto start_time = chrono::high_resolution_clock::now();

        // Simple ring buffer logic
        if(factor == 0) {
            // Maximum speed
            while(keep_running && tick_count < TOTAL_JIFFIES) {
                volatile bool buffer_1_has_space = (ring1->head + 1) % RING_SIZE != ring1->tail;
                volatile bool buffer_2_has_space = (ring2->head + 1) % RING_SIZE != ring2->tail;

                if (buffer_1_has_space && buffer_2_has_space) {
                    // Get timestamp once
                    // auto now = chrono::high_resolution_clock::now();
                    // uint64_t timestamp = chrono::duration_cast<chrono::nanoseconds>(
                    //     now.time_since_epoch()).count();
                    
                    // Write to buffer A
                    // size_t index_1 = ring1->head % RING_SIZE;
                    // ring1->events[index_1].tick_number = tick_count;
                    // ring1->events[index_1].timestamp_ns = timestamp;
                    ring1->head = (ring1->head + 1) % RING_SIZE;
                    
                    // Write to buffer B  
                    // size_t index_2 = ring2->head % RING_SIZE;
                    // ring2->events[index_2].tick_number = tick_count;
                    // ring2->events[index_2].timestamp_ns = timestamp;
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
                volatile bool buffer_1_has_space = (ring1->head + 1) % RING_SIZE != ring1->tail;
                volatile bool buffer_2_has_space = (ring2->head + 1) % RING_SIZE != ring2->tail;

                if (buffer_1_has_space && buffer_2_has_space) {
                    // Get timestamp once
                    // auto now = chrono::high_resolution_clock::now();
                    // uint64_t timestamp = chrono::duration_cast<chrono::nanoseconds>(
                    //     now.time_since_epoch()).count();
                    
                    // Write to buffer A
                    // size_t index_1 = ring1->head % RING_SIZE;
                    // ring1->events[index_1].tick_number = tick_count;
                    // ring1->events[index_1].timestamp_ns = timestamp;
                    ring1->head = (ring1->head + 1) % RING_SIZE;
                    
                    // Write to buffer B  
                    // size_t index_2 = ring2->head % RING_SIZE;
                    // ring2->events[index_2].tick_number = tick_count;
                    // ring2->events[index_2].timestamp_ns = timestamp;
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

        // Reset buffers for new day
        ring1->head = 0;
        ring1->tail = 0;
        ring1->total_generated = 0;
        ring1->dropped_count = 0;
        ring1->producer_running = false;
        ring1->producer_finished = false;
        
        ring2->head = 0;
        ring2->tail = 0;
        ring2->total_generated = 0;
        ring2->dropped_count = 0;
        ring2->producer_running = false;
        ring2->producer_finished = false;

        tick_count = 0;
        successful_writes = 0;
        dropped = 0;

        current_date = current_date.addDays(1);
        total_days++;
        
        if (current_date <= end_date && keep_running) {
            sleep_until_next_9am(); 
        }

    }

    // Cleanup
    munmap(date_config, config_size);
    close(config_fd);
    shm_unlink(shm_config_name);
    
    munmap(ring1, shm_size);
    close(fd1);
    shm_unlink(shm_name1);

    munmap(ring2, shm_size);
    close(fd2);
    shm_unlink(shm_name2);

    if (keep_running) {
        cout << "\n=== SIMULATION COMPLETE ===\n";
        cout << "Total days processed: " << total_days << "\n";
        cout << "Date range: " << start_date.toString() << " to " << end_date.toString() << "\n";
    } else {
        cout << "\n=== SIMULATION INTERRUPTED ===\n";
        cout << "Last processed date: " << current_date.addDays(-1).toString() << "\n";
    }

    return 0;
}
