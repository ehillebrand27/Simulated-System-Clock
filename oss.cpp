#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>

#include <sys/types.h>

#include <sys/ipc.h>

#include <sys/shm.h>

#include <iostream>

#include <sys/wait.h>

#include <time.h>

#include <signal.h>

#include <string.h>

#include <string>



struct PCB 
{
    int occupied; // either true or false
    pid_t pid; // process id of this child
    int startSeconds; // time when it was forked
    int startNano; // time when it was forked
    int endingTimeSeconds; // estimated time it should end
    int endingTimeNano; // estimated time it should end
};


struct PCB processTable[20];


//structure of the clock, will be in shared memory 
struct Clock
{

    int seconds = 0;
    int nanoseconds = 0;

};



const int BUFF_SZ = sizeof(Clock); //size of the shared memory segment
int shm_key; //shared memory key
int shm_id = -1; //shared memory id
Clock *simclock = nullptr; //pointer to the Clock structure in shared memory

volatile sig_atomic_t stopFlag = 0; //set by the signal handler (SIGALRM or SIGINT)


//increments clock by 1000 nanoseconds (tuned so the sim clock is roughly real time)
void incrementClock()
{
    //add 1000ns to clock's nanoseconds
    simclock->nanoseconds += 1000;

    //If nanoseconds reach 1 second, carry 1 second over
    if (simclock->nanoseconds >= 1000000000)
    {
        simclock->seconds++;
        simclock->nanoseconds -= 1000000000;
    }
}


//find an available spot in the process table 
int findAvailablePCB()
{
    // look through processTable
    for(int i = 0; i < 20; i++)
    {
        // if you find an unoccupied slot, return its index
        if(processTable[i].occupied == 0)
        {
            return i;
        }
        
    }

    return -1;
}


//signal handler: just remember which signal arrived, the main loop does the cleanup
void signalHandler(int sig)
{
    stopFlag = sig;
}


//print the whole process table
void printTable()
{
    printf("OSS PID:%d SysClockS: %d SysclockNano: %d\n", getpid(), simclock->seconds, simclock->nanoseconds);
    printf("Process Table:\n");
    printf("Entry Occupied PID StartS StartN EndingTimeS EndingTimeNano\n");

    for (int j = 0; j < 20; j++)
    {
        printf("%d %d %d %d %d %d %d\n", j,
               processTable[j].occupied,
               processTable[j].pid,
               processTable[j].startSeconds,
               processTable[j].startNano,
               processTable[j].endingTimeSeconds,
               processTable[j].endingTimeNano);
    }

    fflush(stdout);
}


//kill all running children and free the shared memory
void cleanup()
{
    for (int j = 0; j < 20; j++)
    {
        if (processTable[j].occupied)
        {
            kill(processTable[j].pid, SIGTERM);
        }
    }

    //reap the killed children
    while (waitpid(-1, NULL, 0) > 0) {}

    if (simclock != nullptr)
    {
        shmdt(simclock);
    }

    if (shm_id != -1)
    {
        shmctl(shm_id, IPC_RMID, NULL);
    }
}



int main(int argc, char *argv[])
{


    int c;

    //default values
    int n = 5;
    float t = 3.0;
    int s = 7;
    float i = 0.1;

    //parse command line arguments 
    while ((c = getopt(argc, argv, "hn:s:t:i:")) != -1)
    {

        switch (c)
        {
        case 'h':
            std::cout << "Usage: ./oss [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInSecondsToLaunchChildren]" << std::endl;
            exit(0);

        case 'n':
            n = std::stoi(optarg);
            break;

        case 't':
            t = std::stof(optarg);
            break;

        case 's':
            s = std::stoi(optarg);
            break;

        case 'i':
            i = std::stof(optarg);
            break;
            
        
        default:
            break;
        }

    }


    std::cout << "OSS starting, PID:" << getpid() << " PPID:" << getppid() << std::endl;
    std::cout << "Called with:\n"
              << "-n " << n << "\n"
              << "-s " << s << "\n"
              << "-t " << t << "\n"
              << "-i " << i << std::endl;

    //basic argument checks
    if (n < 1 || s < 1 || t <= 0 || i < 0)
    {
        std::cerr << "Invalid argument values." << std::endl;
        exit(1);
    }

    //set up signals: SIGALRM after 60 real seconds, and ctrl-c (SIGINT)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigaction(SIGALRM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    alarm(60);



    //generate a key used to identify the shared memory segment
    shm_key = ftok("oss.cpp",0);

    if (shm_key == -1) 
    {
        fprintf(stderr,"Parent:... Error in ftok\n");
        exit(1);
    }

    //create or get the shared memory segment and store its ID
    shm_id = shmget( shm_key , sizeof(Clock) , IPC_CREAT | 0666  );

    if (shm_id == -1) 
    {

        fprintf(stderr,"Shared memory get failed\n");

        exit(1);

    }


    //attach the shared memory segment and get a pointer to it as a Clock
    simclock = (Clock *)shmat(shm_id, 0, 0);

    if (simclock == (void *)-1)
    {
        perror("oss shmat");
        shmctl(shm_id, IPC_RMID, NULL);
        exit(1);
    }

    //intialize the clock to 0
    simclock->seconds = 0;
    simclock->nanoseconds = 0;


    //keep track of running processes and total processes 
    int running = 0;
    int total = 0;



    //the process table starts as empty
    for (int j = 0; j < 20; j++)
    {
        processTable[j].occupied = false;
    }


    //process table only has 20 slots, so never allow more than 20 at once
    if (s > 20)
    {
        s = 20;
    }

    //launch interval in nanoseconds, and the earliest simulated time the next child may launch
    long long launchGap = (long long)(i * 1000000000LL);
    long long nextLaunch = 0;

    //next simulated time to print the process table (every half a simulated second)
    long long nextPrint = 500000000LL;

    //stats for the final report
    int finished = 0;
    long long totalRunNano = 0;


    //seed the random number generator (used to pick each worker's stay time)
    srand(time(NULL) ^ getpid());





    //main oss loop 
    while (total < n || running > 0)
    {



        //did we get a SIGALRM (60 real seconds) or ctrl-c?
        if (stopFlag)
        {
            if (stopFlag == SIGALRM)
            {
                std::cout << "\nOSS: 60 real seconds passed, terminating all children." << std::endl;
            }
            else
            {
                std::cout << "\nOSS: caught ctrl-c, terminating all children." << std::endl;
            }

            cleanup();
            return 1;
        }

        //current simulated time in nanoseconds
        long long now = (long long)simclock->seconds * 1000000000LL + simclock->nanoseconds;

        //every half a simulated second, print the process table
        if (now >= nextPrint)
        {
            printTable();
            nextPrint += 500000000LL;
        }

        //launch only if: more left to launch, fewer than s in the system, and the interval has passed
        if(total < n && running < s && now >= nextLaunch)
        {
            //pick a random stay time between 1 nanosecond and t seconds (simulated time)
            long long maxNano = (long long)(t * 1000000000LL);
            long long r = ((((long long)rand()) << 31) | rand()) % maxNano + 1;
            int workerSeconds = r / 1000000000;
            int workerNanoseconds = r % 1000000000;

            pid_t pid = fork();


            if (pid < 0)
            {
                std::cerr << "Fork failed\n";
                cleanup();
                exit(1);
            }

            else if (pid == 0)
            {
                //create child process
                std::string secondsString = std::to_string(workerSeconds);
                std::string nanoString = std::to_string(workerNanoseconds);

                execl("./worker", "./worker",
                    secondsString.c_str(),
                    nanoString.c_str(),
                    nullptr);

                //if execl fails
                std::cerr << "Exec failed\n";
                exit(1);

            }

            else    
            {

                //find an available process control block 
                int slot = findAvailablePCB();
                if (slot == -1)
                {
                    std::cerr << "No available PCB slot\n";
                    cleanup();
                    exit(1);
                }

                processTable[slot].occupied = true;
                processTable[slot].pid = pid;

                processTable[slot].startSeconds = simclock->seconds;
                processTable[slot].startNano = simclock->nanoseconds;

                // Calculate ending time
                processTable[slot].endingTimeSeconds = processTable[slot].startSeconds + workerSeconds;

                processTable[slot].endingTimeNano = processTable[slot].startNano + workerNanoseconds;

                if (processTable[slot].endingTimeNano >= 1000000000)
                {
                    processTable[slot].endingTimeSeconds++;
                    processTable[slot].endingTimeNano -= 1000000000;
                }


                //parent 
                total++;
                running++;
                nextLaunch = now + launchGap;
                std::cout << "OSS: Launched child PID " << pid
                            << " (" << total << " of " << n << " total launched)"
                            << std::endl;
            }
        }


        // advance the simulated clock
        incrementClock();


        /*
        //debug line
        std::cout << "Clock: "
          << simclock->seconds << "."
          << simclock->nanoseconds << std::endl;
        */

        // check whether the worker has terminated
        int status;
        pid_t result = waitpid(-1, &status, WNOHANG);



        if (result > 0)
        {
            std::cout << "Worker " << result << " has terminated at "
                    << simclock->seconds << " seconds, "
                    << simclock->nanoseconds << " nanoseconds."
                    << std::endl;


            for(int i = 0; i < 20; i++)
            {
                if(processTable[i].occupied && processTable[i].pid == result)
                {
                    //add how long this worker was in the system to the total
                    long long startNano = (long long)processTable[i].startSeconds * 1000000000LL + processTable[i].startNano;
                    totalRunNano += (long long)simclock->seconds * 1000000000LL + simclock->nanoseconds - startNano;
                    finished++;

                    //clear out the entry so the slot can be reused
                    processTable[i].occupied = false;
                    processTable[i].pid = 0;
                    processTable[i].startSeconds = 0;
                    processTable[i].startNano = 0;
                    processTable[i].endingTimeSeconds = 0;
                    processTable[i].endingTimeNano = 0;
                    running--;
                    break;
                }
            }
        }

    }


    //final report
    std::cout << "OSS PID:" << getpid() << " Terminating" << std::endl;
    std::cout << finished << " workers were launched and terminated" << std::endl;
    std::cout << "Workers ran for a combined time of " << totalRunNano / 1000000000LL
              << " seconds " << totalRunNano % 1000000000LL << " nanoseconds." << std::endl;

    //free shared memory
    cleanup();

    return 0;
}