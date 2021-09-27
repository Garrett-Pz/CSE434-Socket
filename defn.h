#pragma once

#include <stdio.h>      
#include <sys/socket.h> 
#include <arpa/inet.h>  
#include <stdlib.h>     
#include <string.h>     
#include <unistd.h>
#include <fcntl.h>

#define BUFFERMAX 256     // Longest message to receive

typedef enum{FREE = 1, LEADER, INDHT} State;

struct user{
    char user_name[16];
    char ipAddr[16];
    unsigned short int portFrom;
    unsigned short int portTo;
    unsigned short int portQuery;
    State state;
    struct user* next;
};

struct dht_user{
    char user_name[16];
    char ipAddr[16];
    unsigned short int portFrom;
    unsigned short int portTo;
    unsigned short int portQuery;
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
    int id;
    int ring_size;
    struct dht_user left;
    struct dht_user right;
};
