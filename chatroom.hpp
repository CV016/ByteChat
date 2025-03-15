#include <iostream>
#include "message.hpp"
#include <set>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>
#include <boost/asio.hpp>
#include <deque>

#ifndef CHATROOM_HPP
#define CHATROOM_HPP

using boost::asio::ip::tcp;

class Participant
{

public:
    virtual void deliver(Message &message) = 0;
    virtual void write(Message &message) = 0;
    virtual ~Participant() = default;
};

typedef std::shared_ptr<Participant> ParticipantPointer;

class Room
{

public:
    virtual void join(ParticipantPointer participant);
    virtual void leave(ParticipantPointer participant);
    virtual void deliver(ParticipantPointer participantPointer, Message &message);

private:
    std::deque<Message> messageQueue;
    enum
    {
        MaxParticipants = 100
    };
    std::set<ParticipantPointer> participants;
};

class Session : public Participant, public std::enable_shared_from_this<Session>
{

public:
    Session(tcp::socket s, Room &room);
    void start();
    void deliver(Message &message);
    void write(Message &message);
    void async_read();
    void async_write(std::string messageBody, size_t messageLength);

private:
    tcp::socket clientSocket;
    boost::asio::streambuf buffer;
    Room &room;
    std::deque<Message> messageQueue;
};

#endif CHATROOM_HPP
