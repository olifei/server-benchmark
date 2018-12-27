/*
* Copyright (C) 2018 Intel Corporation
* Authors: Qin, Zhang
*
* This software may be redistributed and/or modified under the terms of
* the GNU General Public License ("GPL") version 2 only as published by the
* Free Software Foundation.
*/
/*
* Set up to get zapped by a machine check (injected elsewhere)
* recovery function reports physical address of new page - so
* we can inject to that and repeat over and over.
* With "-t" flag report physical address of a ".text" (code) page
* so we will test the instruction fault path - otherwise report
* an allocated data page.
*/
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>

#define CE_UCE_RATIO 33

struct parameters {
    int errorNum;
    int injectionNum;
    int detectionNum;
};

static void _parameter_serialize(struct parameters* para, FILE* txt) {
    fprintf(txt, "%d %d %d", para->errorNum, para->injectionNum, para->detectionNum);
    fflush(txt);
};

static void _parameter_deserialize(struct parameters* para, FILE *txt) {
    fscanf(txt, "%d %d %d", &para->errorNum, &para->injectionNum, &para->detectionNum);
};

void inject_error(int address, FILE *txt, char* timeinfo) {
    static int numCE = 0;
    numCE++;
    // inject UCE
    if (numCE % CE_UCE_RATIO == 0) { 
        char Ucommand[40] = "bash ./mce/UCE.sh ";
        char Uaddress_string[15];
        int Uaddress = rand();
        sprintf(Uaddress_string, "%d", Uaddress);
        strcat(Ucommand, Uaddress_string);
        system(Ucommand);
        fflush(txt);
        fprintf(txt, "Injection UCE: the random address is %d, injected at %s\n", Uaddress, timeinfo);
        fflush(txt);
        printf("NOW THE numCE IS %d\n", numCE);
    }
    // inject CE
    char command[40] = "bash ./mce/CE.sh ";
    char address_string[15];
    sprintf(address_string, "%d", address);
    strcat(command, address_string);
    // printf("Main the command is %s\n", command);
    system(command);
    fflush(txt);
    fprintf(txt, "Injection CE: the random address is %d, injected at %s\n", address, timeinfo); 
    fflush(txt);
}

int detect_error(FILE *txt, char* timeinfo) {
    fprintf(txt, "Detection: detectied at %s\n", timeinfo);
    fflush(txt);
    system("python ./cscripts_146/startCscriptsNill.py -a inband");
    fflush(txt);
    return 0;
}

int main(int argc,char *argv[]) {
    FILE *paralog, *statlog, *ierrorlog, *derrorlog;
    pid_t statpid, detectpid, workloadpid;
    time_t rawtime;
    struct tm * timeinfo;
    struct parameters parameter;

    if(argc == 2) { // first launch
        parameter.errorNum = atoi(argv[1]);
        parameter.injectionNum = 0;
        parameter.detectionNum = 0;
        paralog = fopen("../log/para.log", "w");
        _parameter_serialize(&parameter, paralog);
        fclose(paralog);
    }
    else { // restart launch
        paralog = fopen("../log/para.log", "r");
        _parameter_deserialize(&parameter, paralog);
        fclose(paralog);
    }
    
    // init random error addresses
    int restError = parameter.errorNum - parameter.injectionNum;
    if(restError == 0)
        exit(EXIT_SUCCESS);
    int randnum[restError];
    printf("Main process: # of rest error is %d\n", restError);
    srand(time(0));
    for(int i = 0; i < restError; i++) {
        randnum[i] = rand();
    }

    // process for workload
    workloadpid = fork();
    if (workloadpid < 0) {
        exit(EXIT_FAILURE);
    }
    if (workloadpid == 0) { // workload
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        char *workload = "sh ./workload.sh";
        system(workload);
        exit(EXIT_SUCCESS);
    }

    // process for monitoring status
    statpid = fork();
    if(statpid < 0) {
        exit(EXIT_FAILURE);
    }
    if(statpid == 0) { // status
        statlog = fopen("../log/stat.log", "a");
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        fprintf(statlog, "start at %spid is %d\n", asctime(timeinfo), getpid());
        fflush(statlog);
        while(restError) {
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            fprintf(statlog, "alive at %s\n", asctime(timeinfo));
            fflush(statlog);
            paralog = fopen("../log/para.log", "r");
            _parameter_deserialize(&parameter, paralog); // read log
            fclose(paralog);
            restError = parameter.errorNum - parameter.injectionNum;
            printf("Monitor process: # of rest error is %d\n", restError);
            fflush(stdout);
            if(restError == 0)
                break;
            sleep(30); //wait 30 seconds
        }
        fclose(statlog);
        exit(EXIT_SUCCESS);
    }

    // process for errors detection
    detectpid = fork();
    if(detectpid < 0) {
        exit(EXIT_FAILURE);
    }
    if(detectpid == 0) { // detection
        //close(STDIN_FILENO);
        //close(STDOUT_FILENO);
        //close(STDERR_FILENO);
        derrorlog = fopen("../log/derror.log", "a");
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        fflush(derrorlog);
        while(restError) {
            paralog = fopen("../log/para.log", "r");
            _parameter_deserialize(&parameter, paralog); // read log
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            parameter.detectionNum = detect_error(derrorlog, asctime(timeinfo));
            _parameter_serialize(&parameter, paralog); // write log
            fclose(paralog);
            restError = parameter.errorNum - parameter.injectionNum;
            if(restError == 0)
                break;
            sleep(600); //wait 10 mins
        }
        fclose(derrorlog);
        exit(EXIT_SUCCESS);
    }
    
    // main process for errors injection
    ierrorlog = fopen("../log/ierror.log", "a");
    for(int i= 0; restError > 0; i++) {
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        inject_error(randnum[i], ierrorlog, asctime(timeinfo));
        parameter.injectionNum++;
        // parameter.detectionNum = detect_error(ierrorlog, asctime(timeinfo));
        restError = parameter.errorNum - parameter.injectionNum;
        printf("Main process: # of rest error is %d\n", restError);
        fflush(stdout);
        paralog = fopen("../log/para.log", "w");
        _parameter_serialize(&parameter, paralog); // write log
        fclose(paralog);
        if(restError == 0)
            break;
        sleep(rand()%432); // average 100 errors during 6 hours
        /*for example, 6hours=6*60*60=21600seconds
                       21600/100 = 216seconds/error
          so the expectation for each error is 216s
          we set rand()%(2*216) to get that expectation, which is rand()%432
        */
    }
    fclose(ierrorlog);
//    kill(workloadpid, SIGKILL);
    exit(EXIT_SUCCESS);
}


