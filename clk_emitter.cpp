#include <chrono>
#include <iomanip>
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
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std;

using Clock = chrono::steady_clock;

constexpr uint64_t JIFFIES_PER_SEC = 1 << 16;            // 65,536 jiffies/sec
constexpr uint64_t START_TIME_SEC = 9 * 3600;
constexpr uint64_t END_TIME_SEC = 15 * 3600 + 30 * 60;
constexpr uint64_t TOTAL_SECONDS = END_TIME_SEC - START_TIME_SEC;
volatile uint64_t TOTAL_JIFFIES = TOTAL_SECONDS * JIFFIES_PER_SEC;
constexpr size_t RECORD_SIZE = 88;
volatile bool keep_running = true;
string filename = "Data/sorted_filtered_data.DAT";

// -----------------------------------------------------------------------------------------------------

unordered_map<uint64_t, vector<string>> preprocess_jiffi_map(const string& filename) {
    cout<<"Preprocessing data\n";
    unordered_map<uint64_t, vector<string>> jiffi_map;

    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Failed to open file.\n";
        return jiffi_map;
    }

    string line(RECORD_SIZE, '\0');

    while (file.read(&line[0], RECORD_SIZE)) {
        uint64_t jiffi = stoull(line.substr(22, 14));  // Extract jiffi
        jiffi_map[jiffi].push_back(line);         // Group lines by jiffi
    }
    return jiffi_map;
}

// -----------------------------------------------------------------------------------------------------

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
        // cout<<time / (24 * 3600)<<endl;
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

    bool operator<(const Date& other) const {
        // cout<<toDaysSinceEpoch()<<"  "<<other.toDaysSinceEpoch()<<endl;
        return toDaysSinceEpoch() < other.toDaysSinceEpoch();
    }
    
    string toString() const {
        ostringstream oss;
        oss << year << "-" << setfill('0') << setw(2) << month << "-" << setw(2) << day;
        return oss.str();
    }
};

// -----------------------------------------------------------------------------------------------------

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

// -----------------------------------------------------------------------------------------------------

// Get number of jiffies from Jan 1, 1980 to virtual today 9:00 AM
uint64_t jiffies_from_1980_to_virtual_day(const Date& target_date) {
    // Epoch: 1980-01-01 00:00:00
    tm base_tm = {};
    base_tm.tm_year = 1980 - 1900;
    base_tm.tm_mon = 0;
    base_tm.tm_mday = 1;
    base_tm.tm_hour = 0;
    base_tm.tm_min = 0;
    base_tm.tm_sec = 0;
    base_tm.tm_isdst = -1;
    time_t base_time = mktime(&base_tm);

    // Virtual 9:00 AM of target date
    tm day_tm = {};
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

// -----------------------------------------------------------------------------------------------------

void sleep_until_next_9am() {
    cout << "[INFO] Sleeping until next market day...\n";
    this_thread::sleep_for(chrono::seconds(10)); 
}

// -----------------------------------------------------------------------------------------------------

void printUsage(const char* program_name) {
    cout << "Usage: " << program_name << " <start_date> <end_date>\n";
    cout << "Date format: YYYY-MM-DD\n";
    cout << "Example: " << program_name << " 2024-09-02 2024-09-30\n";
}

// -----------------------------------------------------------------------------------------------------

void handle_sigint(int) {
    keep_running = false;
}

int main(int argc, char* argv[]) {

    signal(SIGINT, handle_sigint);

    // -----------------------------------------------------------------------------------------------------

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

    // -----------------------------------------------------------------------------------------------------

    auto jiffi_records = preprocess_jiffi_map(filename);

    // -----------------------------------------------------------------------------------------------------

    vector<string> batch_records;
    constexpr int RECORD_SIZE = 88;
    string line(RECORD_SIZE, '\0');
    string jiffy_str = "";
    uint64_t jiffies = 0;
    string symbol_raw = "";
    
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "Failed to open file\n";
        return 1;
    }

    string target_symbol1 = "ADANIENSOL";  
    string target_symbol2 = "bBAJAJHIND";

    // -----------------------------------------------------------------------------------------------------

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed");
        return 1;
    }

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &dest.sin_addr);

    // -----------------------------------------------------------------------------------------------------

    volatile uint64_t factor = 0;
    volatile uint64_t ticks = 0;
    volatile uint64_t found = 0;
    volatile uint64_t sent = 0;
    volatile uint64_t base_jiffi = 0;
    uint64_t current_jiffi = 0;

    // -----------------------------------------------------------------------------------------------------

    while(current_date <= end_date && keep_running){

        ticks = 0;
        found = 0;
        sent = 0;

        base_jiffi = jiffies_from_1980_to_virtual_day(current_date);
        current_jiffi = base_jiffi;
        TOTAL_JIFFIES = base_jiffi + TOTAL_SECONDS * JIFFIES_PER_SEC;
        cout << "-----------------------------------------------------------------------------------------------------" << endl << endl;
        cout << "Jiffies before today start: " << base_jiffi << endl;
        cout << "Starting tick generation for " << current_date.toString() << "...\n";

        auto start_time = chrono::high_resolution_clock::now();

        if(factor==0){
            for(;keep_running && current_jiffi <= TOTAL_JIFFIES;){

                batch_records.clear();
                // while (file.read(&line[0], RECORD_SIZE)) {
                //     jiffy_str = line.substr(22, 14);
                //     jiffies = stoull(jiffy_str);

                //     if (jiffies < current_jiffi) {
                //         continue; 
                //     } else if (jiffies == current_jiffi) {
                //         batch_records.push_back(line);
                //         found++;
                //         // symbol_raw = line.substr(38, 10);

                //         // if (symbol_raw == target_symbol1 || symbol_raw == target_symbol2) {
                //         //     batch_records.push_back(line);
                //         //     found++;
                //         // }
                //     } else {
                //         // cout<<jiffies<<endl;
                //         file.seekg(-RECORD_SIZE, ios::cur);
                //         break;
                //     }
                // }

                auto it = jiffi_records.find(current_jiffi);
                if (it != jiffi_records.end()) {
                    batch_records = it->second;
                } 
                
                if (!batch_records.empty()) {
                    string payload;
                    for (const auto& rec : batch_records) {
                        payload += rec;
                    }
                    ssize_t s = sendto(sock, payload.data(), payload.size(), 0, (sockaddr*)&dest, sizeof(dest));
                    if(s>0){
                        sent++;
                    }
                }

                current_jiffi++;
            }
        }else{
            for(;keep_running && current_jiffi < TOTAL_JIFFIES;){

                batch_records.clear();
                while (file.read(&line[0], RECORD_SIZE)) {
                    jiffy_str = line.substr(22, 14);
                    jiffies = stoull(jiffy_str);

                    if (jiffies < current_jiffi) {
                        continue; 
                    } else if (jiffies == current_jiffi) {
                        batch_records.push_back(line);
                        found++;
                        // symbol_raw = line.substr(38, 10);

                        // if (symbol_raw == target_symbol1 || symbol_raw == target_symbol2) {
                        //     batch_records.push_back(line);
                        //     found++;
                        // }
                    } else {
                        file.seekg(-RECORD_SIZE, ios::cur);
                        break;
                    }
                }

                // auto it = jiffi_records.find(current_jiffi);
                // if (it != jiffi_records.end()) {
                //     batch_records = it->second;
                // } 
                
                if (!batch_records.empty()) {
                    string payload;
                    for (const auto& rec : batch_records) {
                        payload += rec;
                    }
                    ssize_t s = sendto(sock, payload.data(), payload.size(), 0, (sockaddr*)&dest, sizeof(dest));
                    if(s>0){
                        sent++;
                    }
                }

                current_jiffi++;

                volatile int i = 0;
                while(i<factor){i++;};
            }
        }
        
        auto end_time = chrono::high_resolution_clock::now();

        // -----------------------------------------------------------------------------------------------------

        ticks = current_jiffi - base_jiffi;

        auto elapsed_ms = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();
        double seconds = elapsed_ms / 1000.0;
        double sim_seconds = ticks / (double)JIFFIES_PER_SEC;

        cout << "\n--- Tick Simulation Ended ---\n";
        cout << "Total Ticks:             " << ticks << "\n";
        cout << "Elapsed Time:            " << seconds << " sec\n";
        cout << "Simulated seconds:       " << sim_seconds << " s\n";
        cout << "Tick Rate:               " << (ticks / seconds) << " ticks/sec\n";
        cout << "Speedup factor observed: " << ((ticks / seconds)/65536) << endl;
        cout << "Found:                   " << found << " \n";
        cout << "Sent:                    " << sent << " \n";
        cout << endl;
        
        // -----------------------------------------------------------------------------------------------------

        current_date = current_date.addDays(1);
        total_days++;

        if (current_date < end_date && keep_running) {
            sleep_until_next_9am(); 
        }

    }

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
