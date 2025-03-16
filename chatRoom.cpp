#include "chatroom.hpp"
#include "encryption.hpp"
#include "logger.hpp"
#include "rate_limiter.hpp"
#include "metrics.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

void Room::join(ParticipantPointer participant){
    this->participants.insert(participant);
}

void Room::leave(ParticipantPointer participant){
    this->participants.erase(participant);
}

void Room::deliver(ParticipantPointer sender, Message &message) {
    // Deliver current message to all other participants
    for (auto participant : participants) {
        if (participant != sender) {
            participant->write(message);
        }
    }
    
    // Store in recent messages queue (optional)
    messageQueue.push_back(message);
    while (messageQueue.size() > 100) {
        messageQueue.pop_front();
    }
}

void Session::async_read() {
    auto self(shared_from_this());
    boost::asio::async_read_until(clientSocket, buffer, "\n",
        [this, self](boost::system::error_code ec, std::size_t bytes_transferred) {
            if (!ec) {
                // Start timing
                MetricsCollector::getInstance().startTimer("message_processing", clientId);
                
                std::string data(boost::asio::buffers_begin(buffer.data()), 
                                boost::asio::buffers_begin(buffer.data()) + bytes_transferred);
                buffer.consume(bytes_transferred);
                
                // Remove newline
                if (!data.empty() && data.back() == '\n') {
                    data.pop_back();
                }
                
                // Check for special commands
                if (data == "!metrics") {
                    // Generate metrics report
                    std::string report = MetricsCollector::getInstance().generateReport();
                    
                    // Send report to client
                    std::string response = "=== METRICS REPORT ===\n" + report + "\n";
                    boost::asio::async_write(clientSocket, 
                        boost::asio::buffer(response),
                        [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                            if (ec) {
                                LOG_ERROR("Error sending metrics to client %s: %s", 
                                          clientId.c_str(), ec.message().c_str());
                            }
                        });
                    
                    // Continue reading
                    async_read();
                    return;
                }
                
                LOG_DEBUG("Received raw data from %s: %s", clientId.c_str(), data.c_str());
                
                // Check rate limit
                if (!RateLimiter::getInstance().checkLimit(clientId)) {
                    LOG_WARNING("Rate limit exceeded for client %s", clientId.c_str());
                    std::string error_msg = "Rate limit exceeded. Please wait before sending more messages.\n";
                    boost::asio::async_write(clientSocket, boost::asio::buffer(error_msg),
                        [](boost::system::error_code, std::size_t) {});
                    
                    // Continue reading
                    async_read();
                    return;
                }
                
                // Create message
                Message message(data);
                
                // Log and deliver message
                std::string msgBody = message.getBody();
                LOG_INFO("Message from %s: %s", clientId.c_str(), msgBody.c_str());
                
                // End timing
                MetricsCollector::getInstance().endTimer("message_processing", clientId);
                
                // Start delivery timing
                MetricsCollector::getInstance().startTimer("message_delivery", clientId);
                
                // Deliver to room
                room.deliver(shared_from_this(), message);
                
                // End delivery timing
                MetricsCollector::getInstance().endTimer("message_delivery", clientId);
                
                // Continue reading
                async_read();
            } else {
                room.leave(shared_from_this());
                if (ec == boost::asio::error::eof) {
                    LOG_INFO("Connection closed by client: %s", clientId.c_str());
                } else {
                    LOG_ERROR("Read error for client %s: %s", 
                              clientId.c_str(), ec.message().c_str());
                }
            }
        }
    );
}

void Session::async_write(std::string messageBody, size_t messageLength) {
    auto self(shared_from_this());
    boost::asio::async_write(clientSocket, 
        boost::asio::buffer(messageBody, messageLength),
        [this, self](boost::system::error_code ec, std::size_t /*bytes_transferred*/) {
            if (!ec) {
                std::cout << "Data is written to the socket" << std::endl;
            } else {
                std::cerr << "Write error: " << ec.message() << std::endl;
            }
        });
}

void Session::start() {
    room.join(shared_from_this());
    async_read();
    
    // Start heartbeat timer
    start_heartbeat_timer();
}

void Session::start_heartbeat_timer() {
    auto self(shared_from_this());
    heartbeat_timer = std::make_unique<boost::asio::steady_timer>(clientSocket.get_executor());
    heartbeat_timer->expires_after(std::chrono::seconds(30)); // Send heartbeat every 30 seconds
    
    heartbeat_timer->async_wait(
        [this, self](const boost::system::error_code& ec) {
            if (!ec) {
                // Send ping message
                std::string ping = "PING\n";
                boost::asio::async_write(clientSocket, 
                    boost::asio::buffer(ping),
                    [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                        if (!ec) {
                            // Schedule next heartbeat
                            start_heartbeat_timer();
                        }
                    });
            }
        });
}

Session::Session(tcp::socket s, Room& r): 
    clientSocket(std::move(s)), 
    room(r) {
    // Generate unique client ID
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    clientId = boost::lexical_cast<std::string>(uuid);
    
    // Enable TCP keepalive
    boost::asio::socket_base::keep_alive option(true);
    clientSocket.set_option(option);
    
    // Log new connection
    auto endpoint = clientSocket.remote_endpoint();
    LOG_INFO("Client connected: %s (IP: %s, Port: %d)", 
             clientId.c_str(),
             endpoint.address().to_string().c_str(),
             endpoint.port());
    
    // Initialize metrics
    MetricsCollector::getInstance().recordMetric("active_connections", 1);
}

Session::~Session() {
    LOG_INFO("Client disconnected: %s", clientId.c_str());
    MetricsCollector::getInstance().recordMetric("active_connections", -1);
}

void Session::write(Message &message) {
    // Encrypt the message if needed
    std::string body = message.getBody();
    bool write_in_progress = !messageQueue.empty();
    messageQueue.push_back(message);
    
    if (!write_in_progress) {
        do_write();
    }
}

void Session::do_write() {
    auto self(shared_from_this());
    
    if (messageQueue.empty()) {
        return;
    }
    
    Message& message = messageQueue.front();
    bool header_decode = message.decodeHeader();
    
    if (header_decode) {
        std::string body = message.getBody();
        body += "\n"; // Add newline for client detection
        
        // Start write timing
        MetricsCollector::getInstance().startTimer("message_write", clientId);
        
        boost::asio::async_write(clientSocket, 
            boost::asio::buffer(body, body.length()),
            [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                // End write timing
                MetricsCollector::getInstance().endTimer("message_write", clientId);
                
                if (!ec) {
                    messageQueue.pop_front();
                    if (!messageQueue.empty()) {
                        do_write();
                    }
                } else {
                    LOG_ERROR("Write error for client %s: %s", 
                              clientId.c_str(), ec.message().c_str());
                    room.leave(shared_from_this());
                }
            });
    } else {
        LOG_WARNING("Message length exceeds the max length for client %s", clientId.c_str());
        messageQueue.pop_front();
        if (!messageQueue.empty()) {
            do_write();
        }
    }
}

void Session::deliver(Message& incomingMessage){
    room.deliver(shared_from_this(), incomingMessage);
}
using boost::asio::ip::address_v4;

void accept_connection(boost::asio::io_context &io, char *port,tcp::acceptor &acceptor, Room &room, const tcp::endpoint& endpoint) {
    tcp::socket socket(io);
    acceptor.async_accept([&](boost::system::error_code ec, tcp::socket socket) {
        if(!ec) {
            std::shared_ptr<Session> session = std::make_shared<Session>(std::move(socket), room);
            session->start();
        }
        accept_connection(io, port,acceptor, room, endpoint);
    });
}


int main(int argc, char *argv[]) {
    try {
        if(argc < 2) {
            std::cerr << "Usage: server <port> [<port> ...]\n";
            return 1;
        }
        
        // Initialize logging with file truncation
        Logger::getInstance().setLogFile("chat_server.log", true); // true = truncate existing log
        Logger::getInstance().setLogLevel(INFO);
        LOG_INFO("Server starting up...");
        
        // Initialize encryption with a password
        std::string encryptionKey = "YourSecretKey123";
        if (!Encryption::initialize(encryptionKey)) {
            LOG_ERROR("Failed to initialize encryption");
            return 1;
        }
        
        // Set rate limit (messages per second)
        RateLimiter::getInstance().setRateLimit(5.0);  // 5 messages per second
        
        // Start metrics reporting
        MetricsCollector::getInstance().startReporting(60, [](const std::string& report) {
            LOG_INFO("Performance Report:\n%s", report.c_str());
        });
        
        Room room;
        boost::asio::io_context io_context;
        tcp::endpoint endpoint(tcp::v4(), atoi(argv[1]));
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), std::atoi(argv[1])));
        
        LOG_INFO("Server started on port %s", argv[1]);
        
        // In main function, add a check for --metrics command
        if (argc > 1 && std::string(argv[1]) == "--metrics") {
            // Print metrics and exit
            std::cout << MetricsCollector::getInstance().generateReport() << std::endl;
            return 0;
        }
        
        accept_connection(io_context, argv[1], acceptor, room, endpoint);
        io_context.run();
    }
    catch (std::exception& e) {
        LOG_ERROR("Exception: %s", e.what());
    }
    
    return 0;
}