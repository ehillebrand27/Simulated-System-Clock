#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>

#include <sys/types.h>

#include <sys/ipc.h>

#include <sys/shm.h>

#include <iostream>

struct PCB 
{
    int occupied; // either true or false
    pid_t pid; // process id of this child
    int startSeconds; // time when it was forked
    int startNano; // time when it was forked
    int endingTimeSeconds; // estimated time it should end
    int endingTimeNano; // estimated time it should end
};


//structure of the clock, will be in shared memory 
struct Clock
{

    int seconds = 0;
    int nanoseconds = 0;

};


const int BUFF_SZ = sizeof(Clock); //size of the shared memory segment
int shm_key; //shared memory key
int shm_id; //shared memory id
Clock *simclock; //pointer to the Clock structure in shared memory


//increments clock by 10ms
void incrementClock()
{
    //add 10ms to clock's nanoseconds
    simclock->nanoseconds += 10000000;

    //If nanoseconds reach 1 second, carry 1 second over
    if (simclock->nanoseconds >= 1000000000)
    {
        simclock->seconds++;
        simclock->nanoseconds -= 1000000000;
    }
}




int main(int argc, char *argv[])
{


    int c;

    //default values
    int n = 5;
    int t = 3;
    int s = 7;
    float i = 0.1;

    //parse command line arguments 
    while ((c = getopt(argc, argv, "hn:s:t:i:")) != -1)
    {

        switch (c)
        {
        case 'h':
            std::cout << "Usage: ./oss [-h] -n 10 -s 3 -t 7" << std::endl;
            exit(0);

        case 'n':
            n = std::stoi(optarg);
            break;

        case 't':
            t = std::stoi(optarg);
            break;

        case 's':
            s = std::stoi(optarg);
            break;

        case 'i':
            i = std::stof(optarg);
            
        
        default:
            break;
        }

    }




    //generate a key used to identify the shared memory segment
    shm_key = ftok("oss.cpp",0);

    if (shm_key <= 0 ) 
    {
        fprintf(stderr,"Parent:... Error in ftok\n");
        exit(1);
    }

    //create or get the shared memory segment and store its ID
    shm_id = shmget( shm_key , sizeof(Clock) , IPC_CREAT | 0666  );

    if (shm_id <= 0) 
    {

        fprintf(stderr,"Shared memory get failed\n");

        exit(1);

    }


    //attach the shared memory segment and get a pointer to it as a Clock
    simclock = (Clock *)shmat(shm_id, 0, 0);

    //intialize the clock to 0
    simclock->seconds = 0;
    simclock->nanoseconds = 0;


    bool run = true;

    //start with one worker

    //keep track of running processes and total processes 
    int running = 0;
    int total = 0;


    pid_t pid = fork();


    if (pid < 0)
    {
        std::cerr << "Fork failed\n";
        exit(1);
    }
    else if (pid == 0)
    {
        //create child process
        std::string tString = std::to_string(t);
        execl("./user", "./user", tString.c_str(), nullptr);

        //if execl fails
        std::cerr << "Exec failed\n";
        exit(1);

    }

    else        
    {
        //parent 
        total++;
        running++;
        std::cout << "OSS: Launched child PID " << pid
                      << " (" << total << " of " << n << " total launched)"
                      << std::endl;
    }
    



    //main oss loop 
    while(run)
    {




        std::cout << "nanoseconds: " << simclock->nanoseconds << " seconds: " << simclock->seconds << std::endl;
        
        //increment the clock
        incrementClock();
    

    }


    return 0;
}