#ifndef METRICS_HPP
#define METRICS_HPP

#include <string>
#include <chrono>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <algorithm>
#include <numeric>
#include <thread>
#include <atomic>
#include <functional>

class MetricsCollector {
public:
    static MetricsCollector& getInstance() {
        static MetricsCollector instance;
        return instance;
    }
    
    // Start a timer for an operation
    void startTimer(const std::string& operation, const std::string& id) {
        std::lock_guard<std::mutex> lock(mtx);
        timers[operation + "_" + id] = std::chrono::high_resolution_clock::now();
    }
    
    // End a timer and record the duration
    void endTimer(const std::string& operation, const std::string& id) {
        std::lock_guard<std::mutex> lock(mtx);
        
        auto now = std::chrono::high_resolution_clock::now();
        auto it = timers.find(operation + "_" + id);
        if (it == timers.end()) return;
        
        auto start = it->second;
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        
        metrics[operation].push_back(duration);
        timers.erase(it);
    }
    
    // Record a metric directly
    void recordMetric(const std::string& name, double value) {
        std::lock_guard<std::mutex> lock(mtx);
        metrics[name].push_back(value);
    }
    
    // Get summary statistics for a metric
    struct MetricStats {
        double min;
        double max;
        double avg;
        double p95;  // 95th percentile
        double p99;  // 99th percentile
        size_t count;
    };
    
    MetricStats getStats(const std::string& name) {
        std::lock_guard<std::mutex> lock(mtx);
        
        if (metrics.find(name) == metrics.end() || metrics[name].empty()) {
            return MetricStats{0, 0, 0, 0, 0, 0};
        }
        
        auto& values = metrics[name];
        size_t count = values.size();
        
        // Calculate basic statistics
        double sum = std::accumulate(values.begin(), values.end(), 0.0);
        double avg = sum / count;
        
        // Sort for percentiles
        std::vector<double> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        
        double min = sorted.front();
        double max = sorted.back();
        
        // Calculate percentiles
        size_t p95_idx = static_cast<size_t>(count * 0.95);
        size_t p99_idx = static_cast<size_t>(count * 0.99);
        
        double p95 = sorted[p95_idx];
        double p99 = sorted[p99_idx];
        
        return MetricStats{min, max, avg, p95, p99, count};
    }
    
    // Start periodic reporting
    void startReporting(int intervalSeconds, 
                        std::function<void(const std::string&)> reportCallback) {
        if (reporterRunning) return;
        
        reporterRunning = true;
        reporterThread = std::thread([this, intervalSeconds, reportCallback]() {
            while (reporterRunning) {
                std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
                
                std::string report = generateReport();
                reportCallback(report);
            }
        });
    }
    
    void stopReporting() {
        reporterRunning = false;
        if (reporterThread.joinable()) {
            reporterThread.join();
        }
    }
    
    std::string generateReport() {
        std::lock_guard<std::mutex> lock(mtx);
        
        std::stringstream ss;
        ss << "=== Performance Metrics Report ===\n";
        
        for (const auto& entry : metrics) {
            if (entry.second.empty()) continue;
            
            auto stats = getStats(entry.first);
            
            ss << entry.first << " (count: " << stats.count << "):\n"
               << "  Min: " << stats.min << " μs\n"
               << "  Avg: " << stats.avg << " μs\n"
               << "  Max: " << stats.max << " μs\n"
               << "  P95: " << stats.p95 << " μs\n"
               << "  P99: " << stats.p99 << " μs\n";
        }
        
        return ss.str();
    }
    
    void clearMetrics() {
        std::lock_guard<std::mutex> lock(mtx);
        metrics.clear();
    }
    
private:
    MetricsCollector() : reporterRunning(false) {}
    ~MetricsCollector() {
        stopReporting();
    }
    
    std::unordered_map<std::string, std::chrono::time_point<std::chrono::high_resolution_clock>> timers;
    std::unordered_map<std::string, std::vector<double>> metrics;
    std::mutex mtx;
    
    std::thread reporterThread;
    std::atomic<bool> reporterRunning;
};

#endif // METRICS_HPP