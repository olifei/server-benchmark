#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>

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
    char command[40] = "bash ./mce/CE.sh ";
    char address_string[15];
    sprintf(address_string, "%d", address);
    strcat(command, address_string);
    // printf("Main the command is %s\n", command);
    system(command);
    fflush(txt);
    fprintf(txt, "Injection: the random address is %d, injected at %s\n", address, timeinfo); 
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
    pid_t statpid, detectpid;
    time_t rawtime;
    struct tm * timeinfo;
    struct parameters parameter;

    if(argc == 2) { // first launch
        parameter.errorNum = atoi(argv[1]);
        parameter.injectionNum = 0;
        parameter.detectionNum = 0;
        paralog = fopen("/var/log/Daemon-sample/para.log", "w");
        _parameter_serialize(&parameter, paralog);
        fclose(paralog);
    }
    else {
        paralog = fopen("/var/log/Daemon-sample/para.log", "r");
        _parameter_deserialize(&parameter, paralog);
        fclose(paralog);
    }

    int restError = parameter.errorNum - parameter.injectionNum;
    int randnum[restError];
    printf("Main process: # of rest error is %d\n", restError);
    srand(time(0));
    for(int i = 0; i < restError; i++) {
        randnum[i] = rand();
    }

    // child process for monitoring status
    statpid = fork();
    if(statpid < 0) {
        exit(EXIT_FAILURE);
    }
    if(statpid > 0) {
        // grand-child process for errors detection
        detectpid = fork();
        if(detectpid < 0) {
            exit(EXIT_FAILURE);
        }
        if(detectpid > 0) {
            derrorlog = fopen("/var/log/Daemon-sample/derror.log", "a");
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            fflush(derrorlog);
            while(restError) {
                paralog = fopen("/var/log/Daemon-sample/para.log", "r");
                _parameter_deserialize(&parameter, paralog);
                time(&rawtime);
                timeinfo = localtime(&rawtime);
                parameter.detectionNum = detect_error(derrorlog, asctime(timeinfo));
                _parameter_serialize(&parameter, paralog);
                fclose(paralog);
                restError = parameter.errorNum - parameter.injectionNum;
                if(restError == 0)
                    break;
                sleep(600); //wait 10 mins
            }
            fclose(derrorlog);
            exit(EXIT_SUCCESS);
        }

        // monitor status
        statlog = fopen("/var/log/Daemon-sample/stat.log", "a");
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        fprintf(statlog, "start at %spid is %d\n", asctime(timeinfo), getpid());
        fflush(statlog);
        while(restError) {
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            fprintf(statlog, "alive at %s\n", asctime(timeinfo));
            fflush(statlog);
            paralog = fopen("/var/log/Daemon-sample/para.log", "r");
            _parameter_deserialize(&parameter, paralog);
            fclose(paralog);
            restError = parameter.errorNum - parameter.injectionNum;
            printf("Child process: # of rest error is %d\n", restError);
            fflush(stdout);
            if(restError == 0)
                break;
            sleep(30); //wait 30 seconds
        }
        fclose(statlog);
        exit(EXIT_SUCCESS);
    }
    
    // main process for errors injection
    ierrorlog = fopen("/var/log/Daemon-sample/ierror.log", "a");
    for(int i= 0; restError > 0; i++) {
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        inject_error(randnum[i], ierrorlog, asctime(timeinfo));
        parameter.injectionNum++;
        // parameter.detectionNum = detect_error(ierrorlog, asctime(timeinfo));
        restError = parameter.errorNum - parameter.injectionNum;
        printf("Main process: # of rest error is %d\n", restError);
        fflush(stdout);
        paralog = fopen("/var/log/Daemon-sample/para.log", "w");
        _parameter_serialize(&parameter, paralog);
        fclose(paralog);
        if(restError == 0)
            break;
        sleep(rand()%216); // 6h for 100 errors
    }
    fclose(ierrorlog);
    exit(EXIT_SUCCESS);
}
