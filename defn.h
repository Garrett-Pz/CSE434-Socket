#pragma once

#include <stdio.h>      
#include <sys/socket.h> 
#include <arpa/inet.h>  
#include <stdlib.h>     
#include <string.h>     
#include <unistd.h>
#include <fcntl.h>

#define BUFFERMAX 1024     // Longest message to receive

typedef enum{FREE = 1, LEADER, INDHT} State;

struct user {
    char user_name[16];
    char ipAddr[16];
    unsigned short int portFrom;
    unsigned short int portTo;
    unsigned short int portQuery;
    State state;
    struct user* next;
};

struct dht_user {
    char user_name[16];
    char ipAddr[16];
    unsigned short int portFrom;
    unsigned short int portTo;
    unsigned short int portQuery;
};

struct dht_entry {
    char countryCode[4];
    char shortName[64];
    char tableName[64];
    char longName[128];
    char alphaCode[3];
    char currency[64];
    char region[32];
    char wbCode[3];
    char latestCensus[254];
    struct dht_entry* next;
};




// Definitions for command structures
// Every command has a structure with fields corresponding to the command parameters
// The first byte of every structure defines which command it is

struct user_register {
    char command;   // command 0
    char user_name[16];
    char ipAddr[16];
    unsigned short int portFrom;
    unsigned short int portTo;
    unsigned short int portQuery;
};

struct deregister {
    char command;   // command 1
    char user_name[16];
};

struct setup {
    char command;   // command 2
    int n;
    char user_name[16];
};

struct set_id {
    char command;   // command 3
    int id;
    int ring_size;
    struct dht_user left;
    struct dht_user right;
};

struct dht_complete {
    char command;   // command 4
    char user_name[16];
};

struct store {
    char command;   // command 5
    struct dht_entry record;
};

struct query_dht {
    char command;   // command 6
    char user_name[16];
    char ipAddr[16];
    unsigned short int portQuery;
};

struct query {
    char command;   // command 7
    char longName[128];
    struct sockaddr_in requesterAddr;
};

struct query_success {
    char command;   // command 8
    struct dht_entry record;
};

struct leave_dht {
    char command;   // command 9
    char user_name[16];
    int ring_size;
};

struct teardown {
    char command;  // command 10
    char FLAG;          // 0: Just remove DHT  1: Do full teardown
};

struct reset_id {
    char command;  // command 11
    int id;
};

struct reset_left {
    char command;  // command 12
    unsigned short int port;
    struct sockaddr_in newAddr;
};

struct reset_right {
    char command;  // command 13
    struct sockaddr_in newAddr;
};

struct rebuild_dht {
    char command;  // command 14
    struct sockaddr_in addr;
};

struct dht_rebuilt {
    char command;  // command 15
    char user_name[16];
    char new_leader[16];
};


