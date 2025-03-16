#ifndef RATE_LIMITER_HPP
#define RATE_LIMITER_HPP

#include <unordered_map>
#include <string>
#include <chrono>
#include <mutex>

class RateLimiter {
public:
    static RateLimiter& getInstance() {
        static RateLimiter instance;
        return instance;
    }
    
    // Check if client can send a message
    bool checkLimit(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mtx);
        
        auto now = std::chrono::steady_clock::now();
        auto& client = clients[clientId];
        
        // First message from this client
        if (client.lastRequest.time_since_epoch().count() == 0) {
            client.lastRequest = now;
            client.messageCount = 1;
            client.tokensAvailable = maxTokens - 1;
            return true;
        }
        
        // Calculate time passed and tokens to add
        std::chrono::duration<double> elapsed = now - client.lastRequest;
        double tokensToAdd = elapsed.count() * tokenRefillRate;
        
        // Update tokens
        client.tokensAvailable = std::min(maxTokens, client.tokensAvailable + tokensToAdd);
        
        // Check if we have a token available
        if (client.tokensAvailable < 1.0) {
            // Rate limit exceeded
            client.rateLimitExceeded++;
            return false;
        }
        
        // Consume a token
        client.tokensAvailable -= 1.0;
        client.lastRequest = now;
        client.messageCount++;
        
        return true;
    }
    
    void setRateLimit(double messagesPerSecond) {
        maxTokens = 5.0;  // Allow burst of 5 messages
        tokenRefillRate = messagesPerSecond;
    }
    
    // Get stats for a client
    struct ClientStats {
        int messageCount = 0;
        int rateLimitExceeded = 0;
    };
    
    ClientStats getClientStats(const std::string& clientId) {
        std::lock_guard<std::mutex> lock(mtx);
        if (clients.find(clientId) == clients.end()) {
            return ClientStats{};
        }
        
        ClientInfo& client = clients[clientId];
        return ClientStats{client.messageCount, client.rateLimitExceeded};
    }
    
private:
    RateLimiter() : maxTokens(5.0), tokenRefillRate(1.0) {}
    
    struct ClientInfo {
        std::chrono::steady_clock::time_point lastRequest;
        int messageCount = 0;
        int rateLimitExceeded = 0;
        double tokensAvailable = 0.0;
    };
    
    std::unordered_map<std::string, ClientInfo> clients;
    std::mutex mtx;
    double maxTokens;
    double tokenRefillRate;
};

#endif // RATE_LIMITER_HPP