#include "defn.h"

void user_register(char*, int, struct sockaddr_in);
void establish_socket(int*, struct sockaddr_in*, int, char*);
void deregister(char*, int, struct sockaddr_in);
void setup_dht(struct dht_user*, int, int);
void set_id(struct dht_user, struct dht_user, struct dht_user, int, int, int);


void DieWithError( const char *errorMessage ) // External error handling function
{
    perror( errorMessage );
    exit(1);
}

void get_line(char* buffer, int len, FILE* in) {    //Modification of fgets that removes trailing newlines
	fgets(buffer, len, in);
	char* newline = strchr(buffer, '\n');
	if (newline) *newline = '\0';
}

int main( int argc, char *argv[] ){
    int sockServ;                    // Socket descriptors
    int sockTo;
    int sockFrom;
    int sockQuery;
    struct sockaddr_in servAddr;     // Server address
    struct sockaddr_in fromAddr;     // Peer addresses
    struct sockaddr_in toAddr;
    struct sockaddr_in queryAddr;
    struct sockaddr_in recvAddr;     // Address from received message
    unsigned int recvAddrLen;        // Length of incoming message
    char msgBuffer[ BUFFERMAX ];     // Buffer for received datagrams

    char buf[64], command[64], *token;    // String buffers to hold command
    int id = -1;                     //DHT identifier. -1 indicates the host is not in a DHT
    int ring_size;
    //struct dht_users*    


    if (argc < 3)    // Test for correct number of arguments
    {
        fprintf( stderr, "Usage: %s <Server IP address> <Echo Port>\n", argv[0] );
        exit( 1 );
    }

    // Create a datagram/UDP socket with server
    if( ( sockServ = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) < 0 )
        DieWithError( "Creation of server socket failed" );

    // Construct the server address structure
    memset( &servAddr, 0, sizeof( servAddr ) ); // Zero out structure
    servAddr.sin_family = AF_INET;                  // Use internet addr family
    servAddr.sin_addr.s_addr = inet_addr( argv[1] ); // Set server's IP address
    servAddr.sin_port = htons( atoi( argv[2] ) );      // Set server's port

    printf("Enter commands:\n");


    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    while(1){
        buf[0] = 1;
        token = "";

        //Check if the process has been sent information from other processes
        if( recvfrom( sockFrom, msgBuffer, BUFFERMAX, MSG_DONTWAIT, (struct sockaddr *) &recvAddr, &recvAddrLen ) != -1) {
            printf("Data recv\n");
        }
        
        // If there are no packets, read from stdin       
        else {
            get_line( buf, 64, stdin );   // Receive command from stdin
            sleep(0.5);
            if(buf[0] != 1){
                strcpy(command, buf);
                token = strtok( buf, " " );
            }
        }

        if( strcmp( token, "register" ) == 0){  // REGISTER COMMAND
            
            strtok(NULL, " ");      // Throwout user name
            char* ip = strtok(NULL, " ");
            int portFrom = atoi(strtok(NULL, " "));
            int portTo = atoi(strtok(NULL, " "));
            int portQuery = atoi(strtok(NULL, " "));


            user_register(command, sockServ, servAddr);   // Send register info to server

            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) // Receive Success/Failure message
                DieWithError( "register: recvfrom() failed" );
            
            printf("%s", (char *) msgBuffer);
            if( strcmp(msgBuffer, "SUCCESS\n") == 0 ){       //If server returns successful, establish the desired sockets
                establish_socket( &sockFrom, &fromAddr, portFrom, ip );   // Establish from, to, query sockets
                establish_socket( &sockTo, &toAddr, portTo, ip );
                establish_socket( &sockQuery, &queryAddr, portQuery, ip );

                printf("%d, %d, %d, %d\n", sockTo, sockFrom, fromAddr.sin_addr.s_addr, ntohs(fromAddr.sin_port));

                char c = 100;
                if( sendto( sockTo, &c, 1, 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != 1 )
       		        DieWithError( "sendto() sent a different number of bytes than expected" );    

                if( recvfrom( sockFrom, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen ) != -1)
                    printf("Data recv\n");

            }

        }

        else if( strcmp(token, "deregister") == 0 ) {   //DEREGISTER COMMAND
            
            deregister(command, sockServ, servAddr);

            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) // Receive Success/Failure message
                DieWithError( "deregister: recvfrom() failed" );
            
            printf("%s", (char *) msgBuffer);
            if( strcmp(msgBuffer, "SUCCESS\n") == 0 ){
                close(sockServ);
                close(sockFrom);
                close(sockTo);
                close(sockQuery);
                exit(0);
            }       

        }

        else if( strcmp(token, "setup-dht") == 0 ) {     //SETUP-DHT COMMAND

            struct setup datagram;

            ring_size = atoi(strtok(NULL, " "));    // Create datagram
            datagram.command = 2;
            datagram.n = ring_size;
            strcpy( datagram.user_name, strtok(NULL, " ") );

            if( sendto( sockServ, &datagram, sizeof(datagram), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(datagram) )  // Send datagram to server
                DieWithError( "setup: sendto() sent a different number of bytes than expected" );

            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) // Receive Success/Failure message
                DieWithError( "setup: recvfrom() failed" );
            
            // If success is received, then receive the list
            printf("%s", (char *) msgBuffer);
            if( strcmp(msgBuffer, "SUCCESS\n") == 0 ) {
                char c = 0;

                if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) // Receive list
                    DieWithError( "setup: recvfrom() failed" );   

                printf("DHT Users Received\n");
                struct dht_user* dht_users = malloc( datagram.n * sizeof(struct dht_user));     // Extract List
                dht_users = (struct dht_user *) msgBuffer;
                
                
                id = 0;     //Make this process the leader
                setup_dht(dht_users, ring_size, sockTo);
            }


        }

        else if( strcmp( token, "stop" ) == 0){
            char c = 100;

            if( sendto( sockServ, &c, 1, 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != 1 )
       		    DieWithError( "sendto() sent a different number of bytes than expected" );

            close(sockServ);
            exit(0);
        }

        else if( strcmp( token, "test" ) == 0){
             char c = 120;

            if( sendto( sockServ, &c, 1, 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != 1 )
       		    DieWithError( "sendto() sent a different number of bytes than expected" );     

                  
        }
    }
}

void user_register(char* command, int sockServ, struct sockaddr_in servAddr){
    struct user_register datagram;  // Structure for sending to server

    strtok(command, " ");           //Parse input and fill fields of datagram
    datagram.command = 0;
    strcpy( datagram.user_name, strtok(NULL, " ") );
    strcpy( datagram.ipAddr, strtok(NULL, " ") );
    datagram.portFrom = atoi( strtok(NULL, " ") );
    datagram.portTo = atoi( strtok(NULL, " ") );
    datagram.portQuery = atoi( strtok(NULL, " ") );

    if( sendto( sockServ, &datagram, sizeof(datagram), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(datagram) )  // Send datagram to server
        DieWithError( "register: sendto() sent a different number of bytes than expected" );

}

void establish_socket(int *sock, struct sockaddr_in* addr, int port, char* ip) {
    if( ( *sock = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) < 0 )
        DieWithError( "Creation of From socket failed" );

    memset( addr, 0, sizeof( addr ) );           // Zero out structure
    addr->sin_family = AF_INET;                  // Internet address family
    addr->sin_addr.s_addr = inet_addr( ip );     // IP address
    addr->sin_port = htons( port );              // Local port

    if( bind( *sock, (struct sockaddr *) addr, sizeof(struct sockaddr_in)) < 0 )
        DieWithError( "Query: bind() failed" );
}

void deregister(char* command, int sockServ, struct sockaddr_in servAddr) {
    struct deregister datagram; // Datagram structure to send to server
    
    strtok(command, " ");    // Fill fields of datagram
    datagram.command = 1;
    strcpy( datagram.user_name, strtok(NULL, " ") );

    if( sendto( sockServ, &datagram, sizeof(datagram), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(datagram) )  // Send datagram to server
        DieWithError( "register: sendto() sent a different number of bytes than expected" );

}

void setup_dht(struct dht_user* users, int n, int sock) {
    //Set ID of each process in ring
    for(int i = 1; i < n; i++){
        set_id( users[i], users[(i-1) % n], users[(i+1) % n], i, n, sock);
    }
}

void set_id(struct dht_user user, struct dht_user left, struct dht_user right, int id, int n, int sock) {
    struct sockaddr_in addr;
    struct set_id mesg;

    // Create address structure of user_i
    memset( &addr, 0, sizeof( addr ) );
    addr.sin_family = AF_INET;              
    addr.sin_addr.s_addr = inet_addr( user.ipAddr ); 
    addr.sin_port = htons( user.portFrom ) ;     

    // Create message to send to user_i
    mesg.id = id;
    mesg.ring_size = n;
    mesg.left = left;
    mesg.right = right;

    if( sendto( sock, &mesg, sizeof(mesg), 0, (struct sockaddr *) &addr, sizeof( addr ) ) != sizeof(mesg)  )
        DieWithError( "set_id: sendto() sent a different number of bytes than expected" );
}
