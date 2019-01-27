#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

using namespace std;


class cWorkSimulator
{
public:

    cWorkSimulator( boost::asio::io_service& io_service)
        : myTimer( new boost::asio::deadline_timer( io_service ))
    {

    }
    void StartWork()
    {
        // simulated work needs 500msecs
        myTimer->expires_from_now(boost::posix_time::microseconds(500));

        myTimer->async_wait(boost::bind(&cWorkSimulator::FinishWork, this));
    }

    void FinishWork()
    {
        static int count;
        count++;
        std::cout << "Completed Job " << count << "\n";

        // start another job
        StartWork();
    }
private:
    boost::asio::deadline_timer * myTimer;
};

class cNonBlockingTCPClient
{
public:
    cNonBlockingTCPClient(
        boost::asio::io_service& io_service,
        const std::string& ip,
        const std::string& port )
        : myIOService( io_service )
        , myIP( ip )
        , myPort( port )
    {

    }
    void Connect();


private:
    boost::asio::io_service& myIOService;
    std::string myIP;
    std::string myPort;
    boost::asio::ip::tcp::tcp::socket * mySocketTCP;
    enum class constatus
    {
        no,                             /// there is no connection
        yes,                            /// connected
        not_yet
    }                       /// Connection is being made, not yet complete
    myConnection;
};

void cNonBlockingTCPClient::Connect()
{
    try
    {
        boost::system::error_code ec;
        boost::asio::ip::tcp::tcp::resolver resolver( myIOService );
        boost::asio::ip::tcp::tcp::resolver::query query(
            myIP,
            myPort );
        boost::asio::ip::tcp::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query,ec);
        if( ec )
            throw std::runtime_error("resolve");
        mySocketTCP = new boost::asio::ip::tcp::tcp::socket( myIOService );
        boost::asio::connect( *mySocketTCP, endpoint_iterator, ec );
        if ( ec || ( ! mySocketTCP->is_open() ) )
        {
            // connection failed
            delete mySocketTCP;
            mySocketTCP = 0;
            myConnection = constatus::no;
            std::cout << "Client Connection failed\n";

        }
        else
        {
            myConnection = constatus::yes;
            std::cout << "Client Connected OK\n";
        }
    }

    catch ( ... )
    {
        std::cout << "Client Connection failed 2\n";
    }
}

int main()
{
    boost::asio::io_service io_service;

    cWorkSimulator theWorkSimulator( io_service );
    theWorkSimulator.StartWork();

    cNonBlockingTCPClient theClient( io_service, "localhost", "5555" );
    theClient.Connect();

    io_service.run();

    return 0;
}
