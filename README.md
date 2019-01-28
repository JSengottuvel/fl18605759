# fl18605759
Demo non-blocking TCP client

Here is the output from a run.  There is no server available and work time has been set to 2 seconds so that things are clearer
```
C:\Users\James\code\bin>fl18605759.exe

Keyboard monitor running

   To pause for user input type 'q<ENTER>
   To connect to server type 'C <ip> <port><ENTER>
   To read from server type 'R <byte count><ENTER>
   To send a pre-defined message to the server type 'W'
   To stop type 'x<ENTER>' ( DO NOT USE ctrlC )

   Don't forget to hit <ENTER>!

Completed Job 1
Completed Job 2
Completed Job 3
q
input was q
Waiting for user input: C or R or W
c localhost 5555
input was c localhost 5555
cNonBlockingTCPClient::CheckForCommand c localhost 5555
Client Connection failed
Completed Job 4
Completed Job 5
Completed Job 6
Completed Job 7
wCompleted Job 8

input was w
cNonBlockingTCPClient::CheckForCommand w
Write Request but no connection
Completed Job 9
Completed Job 10
x
input was x
cNonBlockingTCPClient::CheckForCommand x
Stopping
Event manager finished
```

