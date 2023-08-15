#include <iostream>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <string>
#include <array>
#include <list>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <thread>
#include <mutex>

#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/thread/thread.hpp>

#include "protocol.hpp"

namespace
{
std::string getTimestamp()
{
    time_t t = time(0);
    struct tm * now = localtime(&t);
    std::stringstream ss;
    ss << '[' << std::setw(2) << now->tm_mday << '/' << std::setfill('0')
       << std::setw(2) << (now->tm_mon + 1) << '/' << std::setfill('0')
       << (now->tm_year + 1900) << " | " << std::setfill('0')
       << std::setw(2) << now->tm_hour << ':' << std::setfill('0')
       << std::setw(2) << now->tm_min << ':' << std::setfill('0')
       << std::setw(2) << now->tm_sec << "] ";

    return ss.str();
}

class WorkerThread
{
public:
    static void run(std::shared_ptr<boost::asio::io_service> ioService)
    {
        {
            std::lock_guard < std::mutex > lock(m);
            std::cout << "[THREAD_ID: " << std::this_thread::get_id() << "] Thread starts" << std::endl;
        }

        ioService->run();

        {
            std::lock_guard < std::mutex > lock(m);
            std::cout << "[THREAD_ID: " << std::this_thread::get_id() << "] Thread ends" << std::endl;
        }

    }
private:
    static std::mutex m;
};

std::mutex WorkerThread::m;
}

class ChatMember
{
public:
    virtual ~ChatMember() {}
    virtual void onMessage(std::array<char, MAX_IP_PACK_SIZE> & msg) = 0;
};

class ChatRoom {
public:
    void enter(std::shared_ptr<ChatMember> chatMember, const std::string & nickname)
    {
        __chatMembers.insert(chatMember);
        __nameTable[chatMember] = nickname;
        std::for_each(__recentMsgs.begin(), __recentMsgs.end(),
                      boost::bind(&ChatMember::onMessage, chatMember, _1));
    }

    void leave(std::shared_ptr<ChatMember> chatMember)
    {
        __chatMembers.erase(chatMember);
        __nameTable.erase(chatMember);
    }

    void broadcast(std::array<char, MAX_IP_PACK_SIZE>& msg, std::shared_ptr<ChatMember> chatMember)
    {
        std::string timestamp = getTimestamp();
        std::string nickname = getNickname(chatMember);
        std::array<char, MAX_IP_PACK_SIZE> formattedMsg;

        strcpy(formattedMsg.data(), timestamp.c_str());
        strcat(formattedMsg.data(), nickname.c_str());
        strcat(formattedMsg.data(), msg.data());

        __recentMsgs.push_back(formattedMsg);
        while (__recentMsgs.size() > maxRecentMsgs)
        {
            __recentMsgs.pop_front();
        }

        std::for_each(__chatMembers.begin(), __chatMembers.end(),
                      boost::bind(&ChatMember::onMessage, _1, std::ref(formattedMsg)));
    }

    std::string getNickname(std::shared_ptr<ChatMember> chatMember)
    {
        return __nameTable[chatMember];
    }

private:
    enum { maxRecentMsgs = 100 };
    std::unordered_set<std::shared_ptr<ChatMember>> __chatMembers;
    std::unordered_map<std::shared_ptr<ChatMember>, std::string> __nameTable;
    std::deque<std::array<char, MAX_IP_PACK_SIZE>> __recentMsgs;
};

class RoomMember : public ChatMember,
                    public std::enable_shared_from_this<RoomMember>
{
public:
    RoomMember(boost::asio::io_service& ioService,
                 boost::asio::io_service::strand& strand, ChatRoom& room)
                 : __socket(ioService), __strand(strand), __room(room)
    {
    }

    boost::asio::ip::tcp::socket& socket() { return __socket; }

    void start()
    {
        boost::asio::async_read(__socket,
                                boost::asio::buffer(__nickname, __nickname.size()),
                                __strand.wrap(boost::bind(&RoomMember::nicknameHandler, shared_from_this(), _1)));
    }

    void onMessage(std::array<char, MAX_IP_PACK_SIZE>& msg)
    {
        bool writeInProgress = !__writeMsgs.empty();
        __writeMsgs.push_back(msg);
        if (!writeInProgress)
        {
            boost::asio::async_write(__socket,
                                     boost::asio::buffer(__writeMsgs.front(), __writeMsgs.front().size()),
                                     __strand.wrap(boost::bind(&RoomMember::writeHandler, shared_from_this(), _1)));
        }
    }

private:
    void nicknameHandler(const boost::system::error_code& error)
    {
        if (strlen(__nickname.data()) <= MAX_NICKNAME - 2)
        {
            strcat(__nickname.data(), ": ");
        }
        else
        {
            __nickname[MAX_NICKNAME - 2] = ':';
            __nickname[MAX_NICKNAME - 1] = ' ';
        }

        __room.enter(shared_from_this(), std::string(__nickname.data()));

        boost::asio::async_read(__socket,
                                boost::asio::buffer(__readMsg, __readMsg.size()),
                                __strand.wrap(boost::bind(&RoomMember::readHandler, shared_from_this(), _1)));
    }

    void readHandler(const boost::system::error_code& error)
    {
        if (!error)
        {
            __room.broadcast(__readMsg, shared_from_this());

            boost::asio::async_read(__socket,
                                    boost::asio::buffer(__readMsg, __readMsg.size()),
                                    __strand.wrap(boost::bind(&RoomMember::readHandler, shared_from_this(), _1)));
        }
        else
        {
            __room.leave(shared_from_this());
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
                                         __strand.wrap(boost::bind(&RoomMember::writeHandler, shared_from_this(), _1)));
            }
        }
        else
        {
            __room.leave(shared_from_this());
        }
    }

    boost::asio::ip::tcp::socket __socket;
    boost::asio::io_service::strand& __strand;
    ChatRoom& __room;
    std::array<char, MAX_NICKNAME> __nickname;
    std::array<char, MAX_IP_PACK_SIZE> __readMsg;
    std::deque<std::array<char, MAX_IP_PACK_SIZE> > __writeMsgs;
};

class Server
{
public:
    Server(boost::asio::io_service& ioService,
           boost::asio::io_service::strand& strand,
           const boost::asio::ip::tcp::endpoint& endpoint)
           : __ioService(ioService), __strand(strand), __acceptor(ioService, endpoint)
    {
        run();
    }

private:

    void run()
    {
        std::shared_ptr<RoomMember> newChatMember(new RoomMember(__ioService, __strand, __room));
        __acceptor.async_accept(newChatMember->socket(), __strand.wrap(boost::bind(&Server::onAccept, this, newChatMember, _1)));
    }

    void onAccept(std::shared_ptr<RoomMember> newChatMember, const boost::system::error_code& error)
    {
        if (!error)
        {
            newChatMember->start();
        }

        run();
    }

    boost::asio::io_service& __ioService;
    boost::asio::io_service::strand& __strand;
    boost::asio::ip::tcp::acceptor __acceptor;
    ChatRoom __room;
};


int main(int argc, char* argv[])
{
    try
    {
        if (argc < 2)
        {
            std::cerr << "Appropriate usage: server <port> [<port> ...]\n";
            return 1;
        }

        std::shared_ptr<boost::asio::io_service> ioService(new boost::asio::io_service);
        boost::shared_ptr<boost::asio::io_service::work> work(new boost::asio::io_service::work(*ioService));
        boost::shared_ptr<boost::asio::io_service::strand> strand(new boost::asio::io_service::strand(*ioService));

        std::cout << "[THREAD_ID: " << std::this_thread::get_id() << "]" << " Server starts" << std::endl;

        std::list < std::shared_ptr < Server >> servers;
        
        for (int i = 1; i < argc; ++i)
        {
            boost::asio::ip::tcp::endpoint endpoint(boost::asio::ip::tcp::v4(), std::atoi(argv[i]));
            std::shared_ptr<Server> a_server(new Server(*ioService, *strand, endpoint));
            servers.push_back(a_server);
        }

        boost::thread_group workers;
        for (int i = 0; i < 1; ++i)
        {
            boost::thread * thread = new boost::thread{ boost::bind(&WorkerThread::run, ioService) };

#ifdef __linux__
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(i, &cpuset);
            pthread_setaffinity_np(thread->native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
            workers.add_thread(thread);
        }

        workers.join_all();
    } catch (std::exception& exp)
    {
        std::cerr << "Exception: " << exp.what() << "\n";
    }

    return 0;
}