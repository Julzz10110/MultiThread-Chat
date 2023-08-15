#include <iostream>
#include <cstring>
#include <array>
#include <deque>
#include <thread>
#include <chrono>

#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include "protocol.hpp"


class Client
{
public:
    Client(const std::array<char, MAX_NICKNAME>& nickname,
            boost::asio::io_service& ioService,
            boost::asio::ip::tcp::resolver::iterator endpointIterator) :
            __ioService(ioService), __socket(ioService)
    {

        strcpy(__nickname.data(), nickname.data());
        memset(__readMsg.data(), '\0', MAX_IP_PACK_SIZE);
        boost::asio::async_connect(__socket, endpointIterator, boost::bind(&Client::onConnect, this, _1));
    }

    void write(const std::array<char, MAX_IP_PACK_SIZE>& msg)
    {
        __ioService.post(boost::bind(&Client::writeImpl, this, msg));
    }

    void close()
    {
        __ioService.post(boost::bind(&Client::closeImpl, this));
    }

private:

    void onConnect(const boost::system::error_code& error)
    {
        if (!error)
        {
            boost::asio::async_write(__socket,
                                     boost::asio::buffer(__nickname, __nickname.size()),
                                     boost::bind(&Client::readHandler, this, _1));
        }
    }

    void readHandler(const boost::system::error_code& error)
    {
        std::cout << __readMsg.data() << std::endl;
        if (!error)
        {
            boost::asio::async_read(__socket,
                                    boost::asio::buffer(__readMsg, __readMsg.size()),
                                    boost::bind(&Client::readHandler, this, _1));
        } else
        {
            closeImpl();
        }
    }

    void writeImpl(std::array<char, MAX_IP_PACK_SIZE> msg)
    {
        bool writeInProgress = !__writeMsgs.empty();
        __writeMsgs.push_back(msg);
        if (!writeInProgress)
        {
            boost::asio::async_write(__socket,
                                     boost::asio::buffer(__writeMsgs.front(), __writeMsgs.front().size()),
                                     boost::bind(&Client::writeHandler, this, _1));
        }
    }

    void writeHandler(const boost::system::error_code& error)
    {
        if (!error)
        {
            __writeMsgs.pop_front();
            if (!__writeMsgs.empty())
            {
                boost::asio::async_write(__socket,
                                         boost::asio::buffer(__writeMsgs.front(), __writeMsgs.front().size()),
                                         boost::bind(&Client::writeHandler, this, _1));
            }
        } else
        {
            closeImpl();
        }
    }

    void closeImpl()
    {
        __socket.close();
    }

    boost::asio::io_service& __ioService;
    boost::asio::ip::tcp::socket __socket;
    std::array<char, MAX_IP_PACK_SIZE> __readMsg;
    std::deque<std::array<char, MAX_IP_PACK_SIZE>> __writeMsgs;
    std::array<char, MAX_NICKNAME> __nickname;
};

int main(int argc, char* argv[])
{
    try
    {
        if (argc != 4)
        {
            std::cerr << "Appropriate usage: client <nickname> <host> <port>\n";
            return 1;
        }
        boost::asio::io_service ioService;
        boost::asio::ip::tcp::resolver resolver(ioService);
        boost::asio::ip::tcp::resolver::query query(argv[2], argv[3]);
        boost::asio::ip::tcp::resolver::iterator iterator = resolver.resolve(query);
        std::array<char, MAX_NICKNAME> nickname;
        strcpy(nickname.data(), argv[1]);

        Client cli(nickname, ioService, iterator);

        std::thread thread(boost::bind(&boost::asio::io_service::run, &ioService));

        std::array<char, MAX_IP_PACK_SIZE> msg;

        while (true)
        {
            memset(msg.data(), '\0', msg.size());
            if (!std::cin.getline(msg.data(), MAX_IP_PACK_SIZE - PADDING - MAX_NICKNAME))
            {
                std::cin.clear();
            }
            cli.write(msg);
        }

        cli.close();
        thread.join();
    } catch (std::exception& e)
    {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}