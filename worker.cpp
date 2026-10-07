#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>

#include <sys/types.h>

#include <sys/ipc.h>

#include <sys/shm.h>

#include <iostream>

#include <string>



//structure of the clock, will be in shared memory 
struct Clock
{

    int seconds = 0;
    int nanoseconds = 0;

};


const int BUFF_SZ = sizeof(Clock); //size of the shared memory segment
int shm_key; //shared memory key
int shm_id; //shared memory id
volatile Clock *sim_clock; //pointer to the Clock structure in shared memory (volatile so it is re-read every loop)


//prints the 3 line status message used for starting, every second, and terminating
void printStatus(int nowS, int nowN, int termS, int termN, const std::string &msg)
{
    std::cout << "WORKER PID:" << getpid() << " PPID:" << getppid() << std::endl;
    std::cout << "SysClockS: " << nowS << " SysclockNano: " << nowN
              << " TermTimeS: " << termS << " TermTimeNano: " << termN << std::endl;
    std::cout << msg << std::endl;
}


int main(int argc, char *argv[])
{


    if (argc < 3)
    {
        std::cout << "Usage: ./worker <seconds> <nanoseconds>\n" << std::endl;
        exit(1);
    }

    std::cout << "Worker starting, PID:" << getpid() << " PPID:" << getppid() << std::endl;
    std::cout << "Called with:\nInterval: " << argv[1] << " seconds, "
              << argv[2] << " nanoseconds" << std::endl;

    shm_key = ftok("oss.cpp",0);
    if (shm_key == -1)
    {
        fprintf(stderr,"Child:... Error in ftok\n");
        exit(1);
    }

    shm_id = shmget(shm_key,BUFF_SZ,0666);
    if (shm_id == -1) 
    {
        perror("worker shmget");
        exit(1);
    }


    //attach the shared memory segment and get a pointer to it as a Clock
    sim_clock = (volatile Clock *)shmat(shm_id, 0, 0);

    if (sim_clock == (void *)-1)
    {
        perror("worker shmat");
        exit(1);
    }

    //time the worker started
    int start_seconds = sim_clock->seconds;
    int start_nanoseconds = sim_clock->nanoseconds;


    //seconds termination time
    int seconds = atoi(argv[1]);
    int term_time_seconds = start_seconds + seconds; 

    //nanoseconds termination time 
    int nanoseconds = atoi(argv[2]);
    int term_time_nanoseconds = start_nanoseconds + nanoseconds;

    //carry over 1 second if nanoseconds is >= 1,000,000,000
    if (term_time_nanoseconds >= 1000000000)
    {
        term_time_seconds++;
        term_time_nanoseconds -= 1000000000;
    }

    printStatus(start_seconds, start_nanoseconds, term_time_seconds,
                term_time_nanoseconds, "--Just Starting");


    //loop until workers termination time is equal to the clock time
    int last_seen_seconds = start_seconds;
    bool term_time = false;
    while(!term_time)
    {
        int now_s = sim_clock->seconds;
        int now_n = sim_clock->nanoseconds;

        if (now_s > term_time_seconds ||
            (now_s == term_time_seconds && now_n >= term_time_nanoseconds))
        {
            printStatus(now_s, now_n, term_time_seconds, term_time_nanoseconds, "--Terminating");
            term_time = true;
        }
        else if (now_s != last_seen_seconds)
        {
            //the seconds changed since the last check
            last_seen_seconds = now_s;
            printStatus(now_s, now_n, term_time_seconds, term_time_nanoseconds,
                        "--" + std::to_string(now_s - start_seconds) + " seconds have passed since starting");
        }

    }

    shmdt((void *)sim_clock);

    return 0;
}