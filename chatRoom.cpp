#include "chatroom.hpp"

void Room::join(ParticipantPointer participant)
{
    this->participants.insert(participant);
}

void Room::leave(ParticipantPointer participant)
{
    this->participants.erase(participant);
}

void Room::deliver(ParticipantPointer participant, Message &message)
{
    messageQueue.push_back(message);
    while (!messageQueue.empty())
    {
        Message msg = messageQueue.front();
        messageQueue.pop_front();

        for (ParticipantPointer _participant : participants)
        {
            if (participant != _participant)
            {
                _participant->write(msg);
            }
        }
    }
}

void Session::async_read()
{
    auto self(shared_from_this());
    boost::asio::async_read_until(clientSocket, buffer, "\n",
    [this, self](boost::system::error_code ec, std::size_t bytes_transferred)
    {
        if (!ec)
        {
            std::string data(boost::asio::buffers_begin(buffer.data()),
                            boost::asio::buffers_begin(buffer.data()) + bytes_transferred);
            buffer.consume(bytes_transferred);
            std::cout << "Received: " << data << std::endl;

            Message message(data);
            deliver(message);
            async_read();
        }
        else
        {
            room.leave(shared_from_this());
            if (ec == boost::asio::error::eof)
            {
                std::cout << "Connection closed by peer" << std::endl;
            }
            else
            {
                std::cout << "Read error: " << ec.message() << std::endl;
            }
        }
    });
}

void Session::async_write(std::string messageBody, size_t messageLength)
{
    auto self(shared_from_this());
    auto write_handler = [self](boost::system::error_code ec, std::size_t bytes_transferred)
    {
        if (!ec)
        {
            std::cout << "Data is written to the socket" << std::endl;
        }
        else
        {
            std::cerr << "Write error: " << ec.message() << std::endl;
        }
    };
    boost::asio::async_write(clientSocket, boost::asio::buffer(messageBody, messageLength), write_handler);
}

void Session::start()
{
    room.join(shared_from_this());
    async_read();
}

Session::Session(tcp::socket s, Room &r) : clientSocket(std::move(s)), room(r) {};

void Session::write(Message &message)
{
    bool header_decode = message.decodeHeader();
    if (header_decode)
    {
        std::string body = message.getBody();
        async_write(body, message.getBodyLength());
    }
    else
    {
        std::cout << "Message length exceeds the max length" << std::endl;
    }
}

void Session::deliver(Message &incomingMessage)
{
    room.deliver(shared_from_this(), incomingMessage);
}
using boost::asio::ip::address_v4;

void accept_connection(boost::asio::io_context &io, char *port, tcp::acceptor &acceptor, Room &room, const tcp::endpoint &endpoint)
{
    acceptor.async_accept(
        [&io, port, &acceptor, &room, &endpoint](boost::system::error_code ec, tcp::socket socket)
        {
            if (!ec)
            {
                std::make_shared<Session>(std::move(socket), room)->start();
            }
            accept_connection(io, port, acceptor, room, endpoint);
        });
}

int main(int argc, char *argv[])
{
    try
    {
        if (argc < 2)
        {
            std::cerr << "Usage: server <port>\n";
            return 1;
        }

        boost::asio::io_context io_context;
        Room room;
        
        tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[1]));
        tcp::acceptor acceptor(io_context, endpoint);
        
        std::cout << "Server started on port " << argv[1] << std::endl;
        
        accept_connection(io_context, argv[1], acceptor, room, endpoint);
        io_context.run();
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }
    return 0;
}