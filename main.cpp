#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

using namespace std;

#define MAX_PACKET_SIZE_BYTES 1024

// set work time to 2 seconds
// to slow things down for debugfging purposes
// you can reduce this to 500 for production
#define WORK_TIME_MSECS 2000

class cWorkSimulator
{
public:

    cWorkSimulator( boost::asio::io_service& io_service)
        : myTimer( new boost::asio::deadline_timer( io_service ))
        , myfStop( false )
    {

    }
    void StartWork()
    {
        // simulated work
        myTimer->expires_from_now(boost::posix_time::milliseconds(WORK_TIME_MSECS));

        myTimer->async_wait(boost::bind(&cWorkSimulator::FinishWork, this));
    }

    void FinishWork()
    {
        if( StopGet() )
        {
            std::cout << "Stopping\n";
            return;
        }
        if( ! myfWaitOnUser)
        {
            static int count;
            count++;
            std::cout << "Completed Job " << count << "\n";
        }

        // start another job
        StartWork();

    }
    void WaitOnUserSet()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        myfWaitOnUser = true;
    }
    void WaitOnUserUnSet()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        myfWaitOnUser = false;
    }
    bool WaitOnUserGet()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        return myfWaitOnUser;
    }
    void Stop()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        myfStop = true;
    }
    bool StopGet()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        return myfStop;
    }
private:
    boost::asio::deadline_timer * myTimer;
    std::mutex myMutex;
    bool myfWaitOnUser;
    bool myfStop;
};

class cNonBlockingTCPClient
{
public:
    cNonBlockingTCPClient(
        boost::asio::io_service& io_service )
        : myIOService( io_service )
        , myTimer( new boost::asio::deadline_timer( io_service ))
    {

    }

    /// Check for commands ( connect, read, write )
    void CheckForCommand();

    /** Set command from user ( thread safe )

    This is called from the keyboard monitor in the keyboard monitor thread
    */
    void Command( const std::string& command);

    /** Get command from user ( thread safe )

    This is called from the main thread
    */
    string Command();

    void Connect(
        const std::string& ip,
        const std::string& port);

    void Read( int byte_count );


private:
    boost::asio::io_service& myIOService;
    boost::asio::ip::tcp::tcp::socket * mySocketTCP;
    boost::asio::deadline_timer * myTimer;
    std::string myCommand;
    std::mutex myMutex;
    enum class constatus
    {
        no,                             /// there is no connection
        yes,                            /// connected
        not_yet
    }                       /// Connection is being made, not yet complete
    myConnection;
    unsigned char myRcvBuffer [ MAX_PACKET_SIZE_BYTES ];

    void handle_read(
        const boost::system::error_code& error,
        std::size_t bytes_received );
};

/** Keyboard monitor

    Runs in its own thread

    'x<ENTER'        exit application
    's <Hz><ENTER>   change output buffer clock speed
*/
class cKeyboard
{
public:
    cKeyboard(boost::asio::io_service& io_service )
        : myIOService( io_service )
    {

    }
    void Set( cWorkSimulator& WS )
    {
        myWS = &WS;
    }
    void Set( cNonBlockingTCPClient& TCP )
    {
        myTCP = &TCP;
    }
    void Start();

private:
    boost::asio::io_service& myIOService;
    cWorkSimulator* myWS;
    cNonBlockingTCPClient * myTCP;
};

void cKeyboard::Start()
{
    std::cout << "\nKeyboard monitor running\n\n"
              "   To pause for user input type 'q<ENTER>\n"
              "   To connect to server type 'C <ip> <port><ENTER>\n"
              "   To read from server type 'R <byte count><ENTER>\n"
              "   To stop type 'x<ENTER>' ( DO NOT USE ctrlC )\n\n"
              "   Don't forget to hit <ENTER>!\n\n";

    std::string cmd;
    while( 1 )
    {
        getline( std::cin, cmd );
        std::cout << "input was " << cmd << "\n";
        switch( cmd[0] )
        {

        case 'x':
        case 'X':
            myTCP->Command( cmd );
            myWS->Stop();

            // return, ending the thread
            return;

        case 'q':
        case 'Q':
            std::cout << "Waiting for user input: C or R or W\n";
            myWS->WaitOnUserSet();
            break;

        case 'c':
        case 'C':
        case 'r':
        case 'R':
        case 'w':
        case 'W':

            // register command with TCP client
            myTCP->Command( cmd );

            // user input finished, resume work
            myWS->WaitOnUserUnSet();

            break;

        }
    }
}
void cNonBlockingTCPClient::CheckForCommand()
{
    string cmd = Command();
    if( cmd.length() )
    {
        std::cout << "cNonBlockingTCPClient::CheckForCommand " << cmd << "\n";

        std::stringstream sst(cmd);
        std::vector< std::string > vcmd;
        std::string a;
        while( getline( sst, a, ' ' ) )
            vcmd.push_back(a);

        switch( vcmd[0][0] )
        {
        case 'r':
        case 'R':
            Read( atoi( vcmd[1].c_str()));
            break;
        case 'c':
        case 'C':
            Connect( vcmd[1], vcmd[2] );
            break;
        case 'x':
        case 'X':
            // stop command, return without scheduling another check
            return;
        default:
            std::cout << "Unrecognized command\n";
            break;
        }

        // clear old command
        Command("");
    }

    //schedule next check
    myTimer->expires_from_now(boost::posix_time::milliseconds(500));

    myTimer->async_wait(boost::bind(&cNonBlockingTCPClient::CheckForCommand, this));
}

void cNonBlockingTCPClient::Command( const std::string& command)
{
    std::lock_guard<std::mutex> lck (myMutex);
    myCommand = command;
}
std::string cNonBlockingTCPClient::Command()
{
    std::lock_guard<std::mutex> lck (myMutex);
    return myCommand;
}

void cNonBlockingTCPClient::Connect(
    const std::string& ip,
    const std::string& port)
{
    try
    {
        boost::system::error_code ec;
        boost::asio::ip::tcp::tcp::resolver resolver( myIOService );
        boost::asio::ip::tcp::tcp::resolver::query query(
            ip,
            port );
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
void cNonBlockingTCPClient::Read( int byte_count )
{
    if( myConnection != constatus::yes )
    {
        std::cout << "Read Request but no connection\n";
        return;
    }
    if( byte_count > MAX_PACKET_SIZE_BYTES )
    {
        std::cout << "Too many bytes requested\n";
        return;
    }
    async_read(
        * mySocketTCP,
        boost::asio::buffer(myRcvBuffer, MAX_PACKET_SIZE_BYTES ),
        boost::bind(&cNonBlockingTCPClient::handle_read, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred ));
    std::cout << "waiting for server to reply\n";
}

void cNonBlockingTCPClient::handle_read(
    const boost::system::error_code& error,
    std::size_t bytes_received )
{
    if( error )
    {
        std::cout << "Connection closed\n";
        myConnection = constatus::no;
        return;
    }
    std::cout << bytes_received << "bytes read\n";
}

int main()
{
    boost::asio::io_service io_service;

    cWorkSimulator theWorkSimulator( io_service );

    cNonBlockingTCPClient theClient( io_service );
    theClient.CheckForCommand();
    //theClient.Connect( "localhost", "5555" );
    //theClient.Read();

    // start keyboard monitor
    cKeyboard theKeyBoard( io_service );
    theKeyBoard.Set( theWorkSimulator );
    theKeyBoard.Set( theClient );
    std::thread * threadKeyboard = new std::thread(
        &cKeyboard::Start,
        std::ref(theKeyBoard) );
    std::this_thread::sleep_for (std::chrono::seconds(3));

    theWorkSimulator.StartWork();

    io_service.run();

    std::cout << "Event manager finished\n";

    return 0;
}
