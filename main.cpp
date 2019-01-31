#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

using namespace std;

#define MAX_PACKET_SIZE_BYTES 1024


/** A non-blocking TCP client */
class cNonBlockingTCPClient
{
public:

    /** CTOR
        param[in] io_service the event manager
    */

    cNonBlockingTCPClient(
        boost::asio::io_service& io_service )
        : myIOService( io_service )
    {

    }

    /** Connect to server
        @param[in] ip address of server
        @param[in] port server is listening to for connections

        This does not return until the connection attempt successds or fails.
        The return occurs so quickly that it does not seem wiorthwhile
        to make this non-blocking.

        On successful connection a pre-defined message is sent to the server
        this is non-blocking and when the message has been sent handle_connect_write() will be called
    */
    void Connect(
        const std::string& ip,
        const std::string& port);

    /** read message from server
        @param[in] byte_count to be read

        This is non-blocking, returning immediatly.
        When sufficient bytes arrive from the server
        the method handle_read() will be called
    */
    void Read( int byte_count );

    /** write pre-defined message to server

        This is non-blocking, returning immediatly.
        When write completes
        the method handle_write() will be called
    */
    void Write();


private:
    boost::asio::io_service& myIOService;
    boost::asio::ip::tcp::tcp::socket * mySocketTCP;
    enum class constatus
    {
        no,                             /// there is no connection
        yes,                            /// connected
        not_yet                          /// Connection is being made, not yet complete
    }
    myConnection;
    unsigned char myRcvBuffer [ MAX_PACKET_SIZE_BYTES ];
    unsigned char myConnectMessage[15] {0x02, 0xfd, 00, 0x05, 00, 00, 00, 07, 0x0f, 0x0d, 0x00, 0x00, 0x00, 0x00, 0x00};
    unsigned char myWriteMessage[15] {0x02, 0xfd, 0x80, 0x01, 00, 00, 00, 07, 0x0f, 0x0d, 0xAA, 0xBB, 0x22, 0x11, 0x22};

    void handle_read(
        const boost::system::error_code& error,
        std::size_t bytes_received );

    void handle_connect_write(
        const boost::system::error_code& error,
        std::size_t bytes_sent );

    void handle_write(
        const boost::system::error_code& error,
        std::size_t bytes_sent );
};

class cJob
{
public:
    int myIndex;
    static int myLastIndex;
    int myLength;


    cJob( int length )
        : myLength( length )
    {
        myIndex = ++myLastIndex;
    }

    void Do()
    {
        std::cout << "\t\t\tStarting job " << myIndex << "\n";

        // Simulate doing some work
        // this line can be replaced by code that really does something
        std::this_thread::sleep_for ( std::chrono::milliseconds( myLength ) );

        std::cout << "\t\t\tJob Completed " << myIndex << "\n";
    }
};

int cJob::myLastIndex = 0;

class cWorkSimulator
{
public:

    cWorkSimulator()
        : myTimerCheckNewWork( new boost::asio::deadline_timer( myEventManager ))
        , myfStop( false )
        , myJob( 0 )
    {
        // start work in its own thread
        myWorkThread = new std::thread(
            &cWorkSimulator::StartWorkInOwnThread,
            std::ref(*this) );
    }

    void Job( int length )
    {
        std::lock_guard<std::mutex> lck (myMutex);

        // add to job queue
        myJobQueue.push( new cJob( length ) );

        std::cout << "\t\t\tJob " << myJobQueue.back()->myIndex << " waiting, queue is " << myJobQueue.size()-1 << "\n";
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

    void WorkDone()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        delete myJobQueue.front();
        myJobQueue.pop();
    }

    void Job( cJob* job )
    {
        std::lock_guard<std::mutex> lck (myMutex);
        myJob = job;
    }
    cJob * Job()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        return myJob;
    }
    bool GetJob()
    {
        std::lock_guard<std::mutex> lck (myMutex);
        if( myJobQueue.size() )
        {
            myJob = myJobQueue.front();
            return true;
        }
        return false;
    }
private:
    boost::asio::io_service myEventManager;
    boost::asio::deadline_timer * myTimerCheckNewWork;
    std::mutex myMutex;
    bool myfWaitOnUser;
    bool myfStop;                       // true if stop reuest
    std::thread * myWorkThread;
    cJob * myJob;                       // current Job
    std::queue<cJob*> myJobQueue;       // waiting jobs

    void StartWorkInOwnThread();
    void CheckForNewWork();

};


/** Command handler receives commands from the keyboard monitor ( running in keyboard monitor thread )
    and dispatches them to the TCP client running in the main thread */

class cCommander
{
public:
    cCommander(
        boost::asio::io_service& io_service,
        cNonBlockingTCPClient& TCP,
        cWorkSimulator& Worker )
        : myIOService( io_service )
        , myTCP( TCP )
        , myWorker( Worker )
        , myTimer( new boost::asio::deadline_timer( io_service ))
    {
        CheckForCommand();
    }

    /** Set command from user ( thread safe )

    This is called from the keyboard monitor in the keyboard monitor thread
    */
    void Command( const std::string& command);

    /** Get command from user ( thread safe )

    This is called from the main thread
    */
    string Command();


private:
    boost::asio::io_service& myIOService;
    boost::asio::deadline_timer * myTimer;
    cNonBlockingTCPClient & myTCP;
    cWorkSimulator& myWorker;
    std::string myCommand;
    std::mutex myMutex;

    /// Check for commands ( connect, read, write )
    void CheckForCommand();
};


/** Keyboard monitor

    Runs in its own thread

    'x<ENTER'        exit application
    's <Hz><ENTER>   change output buffer clock speed
*/
class cKeyboard
{
public:
    cKeyboard(
        cCommander& myCommander );

    void Start();

private:
    cCommander * myCommander;
};

cKeyboard::cKeyboard(
    cCommander& Commander
)
    : myCommander( &Commander )
{
    // start monitor in own thread
    new std::thread(
        &cKeyboard::Start,
        std::ref(*this) );

    // allow time for thread to start and user to read usage instructions
    std::this_thread::sleep_for (std::chrono::seconds(3));
}


void cKeyboard::Start()
{
    std::cout << "\nKeyboard monitor running\n\n"
              "   To connect to server type 'C <ip> <port><ENTER>'\n"
              "   To read from server type 'R <byte count><ENTER>'\n"
              "   To send a pre-defined message to the server type 'W'\n"
              "   To submit a job request type 'J <length msecs>'\n"
              "   To stop type 'x<ENTER>' ( DO NOT USE ctrlC )\n\n"
              "   Don't forget to hit <ENTER>!\n\n";

    std::string cmd;
    while( 1 )
    {
        getline( std::cin, cmd );

        switch( cmd[0] )
        {

        case 'x':
        case 'X':
            myCommander->Command( cmd );

            // return, ending the thread
            return;

        case 'q':
        case 'Q':
            std::cout << "Waiting for user input: C or R or W\n";
            // myWS->WaitOnUserSet();
            break;

        case 'c':
        case 'C':
        case 'r':
        case 'R':
        case 'w':
        case 'W':
        case 'j':
        case 'J':

            // register command with TCP client
            myCommander->Command( cmd );

            // user input finished, resume work
            //myWS->WaitOnUserUnSet();

            break;

        }
    }
}
void cCommander::CheckForCommand()
{
    string cmd = Command();

    if( cmd.length() )
    {
        std::cout << "Command: " << cmd << "\n";

        std::stringstream sst(cmd);
        std::vector< std::string > vcmd;
        std::string a;
        while( getline( sst, a, ' ' ) )
            vcmd.push_back(a);

        switch( vcmd[0][0] )
        {
        case 'r':
        case 'R':
            if( vcmd.size() < 2 )
                std::cout << "Read command missing byte count\n";
            else
                myTCP.Read( atoi( vcmd[1].c_str()));
            break;

        case 'c':
        case 'C':
            myTCP.Connect( vcmd[1], vcmd[2] );
            break;

        case 'w':
        case 'W':
            myTCP.Write();
            break;

        case 'j':
        case 'J':
            if( vcmd.size() < 2 )
                std::cout << "Job command missing length\n";
            else
                myWorker.Job( atoi( vcmd[1].c_str()) );
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

    myTimer->async_wait(boost::bind(&cCommander::CheckForCommand, this));
}

void cCommander::Command( const std::string& command)
{
    std::lock_guard<std::mutex> lck (myMutex);
    myCommand = command;
}
std::string cCommander::Command()
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

            boost::asio::async_write(
                *mySocketTCP,
                boost::asio::buffer(myConnectMessage, 15),
                boost::bind(&cNonBlockingTCPClient::handle_connect_write, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred ));
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
    if( byte_count < 1 )
    {
        std::cout << "Error in read command\n";
    }
    if( byte_count > MAX_PACKET_SIZE_BYTES )
    {
        std::cout << "Too many bytes requested\n";
        return;
    }
    async_read(
        * mySocketTCP,
        boost::asio::buffer(myRcvBuffer, byte_count ),
        boost::bind(&cNonBlockingTCPClient::handle_read, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred ));
    std::cout << "waiting for server to reply\n";
}

void cNonBlockingTCPClient::Write()
{
    if( myConnection != constatus::yes )
    {
        std::cout << "Write Request but no connection\n";
        return;
    }
    boost::asio::async_write(
        *mySocketTCP,
        boost::asio::buffer(myWriteMessage, 15),
        boost::bind(&cNonBlockingTCPClient::handle_write, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred ));
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
    std::cout << bytes_received << " bytes read\n";
    for( int k = 0; k < bytes_received; k++ )
        std::cout << std::hex << (int)myRcvBuffer[k] << " ";
    std::cout << std::dec << "\n";
}

void cNonBlockingTCPClient::handle_connect_write(
    const boost::system::error_code& error,
    std::size_t bytes_sent )
{
    if( error || bytes_sent != 15 )
    {
        std::cout << "Error sending connection message to server\n";
        myConnection = constatus::no;
        return;
    }
    std::cout << "Connection message sent to server\n";
}

void cNonBlockingTCPClient::handle_write(
    const boost::system::error_code& error,
    std::size_t bytes_sent )
{
    if( error || bytes_sent != 15 )
    {
        std::cout << "Error sending write message to server\n";
        myConnection = constatus::no;
        return;
    }
    std::cout << "Write message sent to server\n";
}

void cWorkSimulator::StartWorkInOwnThread()
{
    CheckForNewWork();
    myEventManager.run();
}
void cWorkSimulator::CheckForNewWork()
{
    if( StopGet() )
        return;

    if( GetJob() )
    {
        myJob->Do();

        WorkDone();
    }

    // check for new work every 100 msecs
    myTimerCheckNewWork->expires_from_now(boost::posix_time::milliseconds(100));
    myTimerCheckNewWork->async_wait(boost::bind(&cWorkSimulator::CheckForNewWork, this));
}

int main()
{
    // construct event manager
    boost::asio::io_service io_service;

    // construct work simulator
    cWorkSimulator theWorkSimulator;

    // construct TCP client
    cNonBlockingTCPClient theClient( io_service );

    // construct commander to dispatch commands from user in keyboard thread to TCP client in main thread
    cCommander theCommander(
        io_service,
        theClient,
        theWorkSimulator );

    // start keyboard monitor
    cKeyboard theKeyBoard(
        theCommander
    );

    // start event handler ( runs until stop requested )
    io_service.run();

    std::cout << "Event manager finished\n";

    return 0;
}
