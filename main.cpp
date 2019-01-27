#include <iostream>
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

int main()
{
     boost::asio::io_service io_service;

    cWorkSimulator theWorkSimulator( io_service );
    theWorkSimulator.StartWork();

    io_service.run();

    return 0;
}
