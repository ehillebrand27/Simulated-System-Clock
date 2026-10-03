#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>

#include <sys/types.h>

#include <sys/ipc.h>

#include <sys/shm.h>

#include <iostream>



//structure of the clock, will be in shared memory 
struct Clock
{

    int seconds = 0;
    int nanoseconds = 0;

};


const int BUFF_SZ = sizeof(Clock); //size of the shared memory segment
int shm_key; //shared memory key
int shm_id; //shared memory id
Clock *sim_clock; //pointer to the Clock structure in shared memory


int main(int argc, char *argv[])
{


    if (argc < 3)
    {
        std::cout << "Usage: ./worker <seconds> <nanoseconds>\n" << std::endl;
        exit(1);
    }

    shm_key = ftok("oss.cpp",0);
    if (shm_key <= 0 )
    {
        fprintf(stderr,"Child:... Error in ftok\n");
        exit(1);
    }

    shm_id = shmget(shm_key,BUFF_SZ,0666);
    if (shm_id <= 0 ) 
    {
        perror("worker shmget");
        exit(1);
    }


    //attach the shared memory segment and get a pointer to it as a Clock
    sim_clock = (Clock *)shmat(shm_id, 0, 0);

    std::cout << "nanoseconds: " << sim_clock->nanoseconds << " seconds: " << sim_clock->seconds << std::endl;


    //seconds termination time
    int seconds = atoi(argv[1]);
    int term_time_seconds = sim_clock->seconds + seconds; 

    //nanoseconds termination time 
    int nanoseconds = atoi(argv[2]);
    int term_time_nanoseconds = sim_clock->nanoseconds + nanoseconds;

    //carry over 1 second if nanoseconds is >= 1,000,000,000
    if (term_time_nanoseconds >= 1000000000)
    {
        term_time_seconds++;
        term_time_nanoseconds -= 1000000000;
    }


    //loop until workers termination time is equal to the clock time
    bool term_time = false;
    while(!term_time)
    {

        //check if term_time_seconds is >= to the clocks seconds
        if(sim_clock->seconds >= term_time_seconds)
        {
            //if it is then check if term_time_nanoseconds is >= to the clocks nanoseconds
            if(sim_clock->nanoseconds >= term_time_nanoseconds)
            {
                //if both times are >= to the clocks seconds then terminate the worker
                term_time = true;
            }
        }

    }

    return 0;
}