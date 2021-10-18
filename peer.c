#include "defn.h"

//FUNCTION DECLARATIONS
void user_register(char*, int, struct sockaddr_in);
void establish_socket(int*, struct sockaddr_in*, int, char*);
void deregister(char*, int, struct sockaddr_in);
void setup_dht(struct dht_user*, int);
void send_set_id(struct dht_user, struct dht_user, struct dht_user, int, int);
void set_id(struct set_id*);
void populate_dht();
void store(struct dht_entry*);
void dht_insert(struct dht_entry*, int);
int compute_record_pos(char*);
void print_record(struct dht_entry);
void process_query(struct query*);
struct dht_entry* retrieve_record(char*, int);
struct dht_entry copy_record(struct dht_entry*);
void delete_dht();
void delete_dht_list(struct dht_entry*);

//GLOBAL VARS
int sockServ;                   // Socket descriptors
int sockSend;
int sockRecv;
int sockQuery;
struct sockaddr_in servAddr;    // Server address
struct sockaddr_in fromAddr;    // Peer addresses
struct sockaddr_in toAddr;
struct sockaddr_in queryAddr;
struct sockaddr_in recvAddr;    // Address from received message
unsigned int recvAddrLen;       // Length of incoming message
char msgBuffer[ BUFFERMAX ];    // Buffer for received datagrams
char ipAddr[16];                // IP Address of process

char buf[64], command[64], *token;  // String buffers to hold command
char user_name[16];                 // Username of process
int id = -1;                        // DHT identifier. -1 indicates the host is not in a DHT
int ring_size;                      // Size of DHT ring
struct dht_entry** hashTable;       // This processes hash table

//Utility Functions
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

void fill_space(char* c, int i) {
    c[strlen(c) + 5] = '\0';
    for(int j = strlen(c); j > i; j--) {
        c[j + 5] = c[j];
    }

    c[i+1] = 'B';
    c[i+2] = 'L';
    c[i+3] = 'A';
    c[i+4] = 'N';
    c[i+5] = 'K';
}

int read_stats_line(char* buffer, FILE* in) {   //Return a line from the StatsCountry file. Returns 0 at EOF

    //Account for the fact that newlines are marked only by carriage returns
    if( fscanf(in, "%512[ -ӿ]\r", buffer) == EOF) return 0;
    char* cr = strchr(buffer, '\r');
    if (cr) *cr = '\0';

    //Fill in empty spaces for easier tokenization later
    for( int i = 0; buffer[i] != '\0'; i++ ) {
        if( buffer[i] == ',' && (buffer[i+1] == ',' || buffer[i+1] == '\0'))
        fill_space(buffer, i);
    }

    return 1;
}

char* get_token(char* line, char* delim) {  //Tokenizes string from csv file. Keeps strings surrounded by "" intact
    char* token = strtok(line, delim);

    if( token[0] == '\"' ) {
        while( token[strlen(token) - 1] != '\"') {  //Continue concating tokens until second " is reached
            strtok(NULL, delim);
            token[strlen(token)] = ',';             //Reappend comma that is deleted after strtok() call
        }
    }

    return token;
}


//Main Method
int main( int argc, char *argv[] ) {

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

        //Check if the process has been sent information to its Query port
        if( recvfrom( sockQuery, msgBuffer, BUFFERMAX, MSG_DONTWAIT, (struct sockaddr *) &recvAddr, &recvAddrLen ) != -1 ) {
            if( msgBuffer[0] == 3 ) {               // SET-ID COMMAND ------------------------------
                struct set_id* datagram = (struct set_id*) msgBuffer;
                set_id(datagram);
            }

            else if( msgBuffer[0] == 7 ) {          // QUERY COMMAND ------------------------------
                struct query* datagram = (struct query*) msgBuffer;
                process_query(datagram);
            }

        }
        //Check if the process has been sent information to its Recv port
        else if ( recvfrom( sockRecv, msgBuffer, BUFFERMAX, MSG_DONTWAIT, (struct sockaddr *) &recvAddr, &recvAddrLen ) != -1 ) {
            if( msgBuffer[0] == 5 ) {               // STORE COMMAND ------------------------------
                struct store* datagram = (struct store*) msgBuffer;
                struct dht_entry* record = calloc(1, sizeof(struct dht_entry));
                memcpy(record, &(datagram->record), sizeof(struct dht_entry));

                store(record);
            }

            else if( msgBuffer[0] == 7 ) {          // QUERY COMMAND ------------------------------
                struct query* datagram = (struct query*) msgBuffer;
                process_query(datagram);
            }
        
            else if( msgBuffer[0] == 10 ) {         // TEARDOWN COMMAND ------------------------------
                struct teardown* datagram = (struct teardown*) msgBuffer;

                delete_dht();

                // Propagate teardown command around ring
                if( sendto( sockSend, datagram, sizeof(struct teardown), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(struct teardown) ) 
                    DieWithError( "teardown: sendto() sent a different number of bytes than expected" );
            }
        
            else if( msgBuffer[0] == 11 ) {         // RESET-ID COMMAND ------------------------------
                struct reset_id* datagram = (struct reset_id*) msgBuffer;

                id = datagram->id;
                ring_size = datagram->ring_size;
                hashTable = calloc(353, sizeof(struct dht_entry*));

                // Propagate reset_id command around ring
                datagram->id += 1;
                printf("New ID: %d, New Ring Size: %d\n", id, ring_size);
                if( sendto( sockSend, datagram, sizeof(struct reset_id), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(struct reset_id) ) 
                    DieWithError( "teardown: sendto() sent a different number of bytes than expected" );
            }
        
            else if( msgBuffer[0] == 12 ) {         // RESET-LEFT COMMAND ------------------------------
                struct reset_left* datagram = (struct reset_left*) msgBuffer;

                // Check if this process is the left neighbor of the calling process
                if(datagram->port == toAddr.sin_port) {
                    toAddr = datagram->newAddr;
                }
                // If not the left neighbor, propagate message around ring
                else {
                    if( sendto( sockSend, datagram, sizeof(struct reset_left), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(struct reset_left) ) 
                        DieWithError( "reset_left: sendto() sent a different number of bytes than expected" );
                }
            }

            else if( msgBuffer[0] == 13 ) {         // RESET-RIGHT COMMAND ------------------------------
                struct reset_right* datagram = (struct reset_right*) msgBuffer;
            }
        
            else if( msgBuffer[0] == 14 ) {         // REBUILD-DHT COMMAND ------------------------------
                struct rebuild_dht* datagram = (struct rebuild_dht*) msgBuffer;

                // Build DHT
                populate_dht();

                // Send username
                if( sendto( sockSend, user_name, sizeof(user_name), 0, (struct sockaddr *) &datagram->addr, sizeof(datagram->addr) ) != sizeof(user_name) ) 
                    DieWithError( "rebuild_dht: sendto() sent a different number of bytes than expected" );
            }
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


        if( strcmp( token, "register" ) == 0){          // REGISTER COMMAND ------------------------------
            
            strcpy(user_name, strtok(NULL, " "));
            char* ip = strtok(NULL, " ");
            int portFrom = atoi(strtok(NULL, " "));
            int portTo = atoi(strtok(NULL, " "));
            int portQuery = atoi(strtok(NULL, " "));


            user_register(command, sockServ, servAddr);   // Send register info to server

            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) // Receive Success/Failure message
                DieWithError( "register: recvfrom() failed" );
            
            printf("%s", (char *) msgBuffer);
            if( strcmp(msgBuffer, "SUCCESS\n") == 0 ){       //If server returns successful, establish the desired sockets
                //establish_socket( &sockRecv, &fromAddr, portFrom, ip );   // Establish from, to, query sockets
                //establish_socket( &sockSend, &toAddr, portTo, ip );

                strcpy(ipAddr, ip);

                if( ( sockSend = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) < 0 )
                    DieWithError( "Creation of Send socket failed" );       
            
                establish_socket( &sockRecv, &fromAddr, portFrom, ip );
                establish_socket( &sockQuery, &queryAddr, portQuery, ip );
            }

        }

        else if( strcmp(token, "deregister") == 0 ) {   // DEREGISTER COMMAND ------------------------------
            
            deregister(command, sockServ, servAddr);

            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) // Receive Success/Failure message
                DieWithError( "deregister: recvfrom() failed" );
            
            printf("%s", (char *) msgBuffer);
            if( strcmp(msgBuffer, "SUCCESS\n") == 0 ){
                close(sockServ);
                close(sockRecv);
                close(sockSend);
                close(sockQuery);
                exit(0);
            }       

        }

        else if( strcmp(token, "setup-dht") == 0 ) {    // SETUP-DHT COMMAND ------------------------------

            struct setup datagram;
            struct dht_complete msg;

            // Create datagram
            datagram.n = atoi(strtok(NULL, " "));
            datagram.command = 2;
            strcpy( datagram.user_name, strtok(NULL, " ") );

            // Send datagram to server
            if( sendto( sockServ, &datagram, sizeof(datagram), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(datagram) ) 
                DieWithError( "setup: sendto() sent a different number of bytes than expected" );

            // Receive Success/Failure message
            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                DieWithError( "setup: recvfrom() failed" );
            
            // If success is received, then receive the list
            if( strcmp(msgBuffer, "SUCCESS\n") == 0 ) {
                char c = 0;

                // Receive list
                if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 )
                    DieWithError( "setup: recvfrom() failed" );   

                // Extract List
                struct dht_user* dht_users = (struct dht_user *) msgBuffer;
                
                // Setup DHT
                ring_size = datagram.n;
                setup_dht( dht_users, ring_size );

                // Send dht-complete message
                msg.command = 4;
                strcpy( msg.user_name, datagram.user_name );
                if( sendto( sockServ, &msg, sizeof(msg), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(msg) )  // Send datagram to server
                    DieWithError( "dht-complete: sendto() sent a different number of bytes than expected" );


                //Receive Success/Failure
                if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) // Receive Success/Failure message
                    DieWithError( "deregister: recvfrom() failed" );           
                printf("%s", (char *) msgBuffer);
            }


        }

        else if( strcmp(token, "query-dht") == 0) {     // QUERY-DHT COMMAND ------------------------------
            
            struct query_dht datagram;
            struct query_dht* response;
            struct query query;
            struct sockaddr_in dhtNode;
            char queryName[128];
            struct query_success* fullRecord;

            // Create Datagram
            datagram.command = 6;
            strcpy( datagram.user_name, strtok(NULL, " ") );

            // Send datagram to server
            if( sendto( sockServ, &datagram, sizeof(datagram), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(datagram) ) 
                DieWithError( "query-dht: sendto() sent a different number of bytes than expected" );

            // Receive Success/Failure message
            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                DieWithError( "query-dht: recvfrom() failed" );


            if( strcmp(msgBuffer, "FAILURE\n") == 0 ) {
                printf("%s", (char *) msgBuffer);
            }
            // If success is received, send the query
            else {
                // Extract info of intial node to query
                response = (struct query_dht*) msgBuffer;

                memset( &dhtNode, 0, sizeof( dhtNode ) );
                dhtNode.sin_family = AF_INET;
                dhtNode.sin_addr.s_addr = inet_addr( response->ipAddr ); 
                dhtNode.sin_port = htons( response->portQuery );      
                
                // Create query datagram
                query.command = 7;

                printf("Enter long name to serach for: ");
                fcntl(0, F_SETFL, fcntl(0, F_GETFL) & (~ O_NONBLOCK));
                get_line( queryName, 128, stdin );
                fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);

                strcpy(query.longName, queryName);
                query.requesterAddr = queryAddr;

                // Send query to initial node
                if( sendto( sockQuery, &query, sizeof(query), 0, (struct sockaddr *) &dhtNode, sizeof( dhtNode ) ) != sizeof(query) ) 
                    DieWithError( "query: sendto() sent a different number of bytes than expected" );

                // Receive Success/Failure message
                if( ( recvfrom( sockQuery, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                    DieWithError( "query: recvfrom() failed" );
                
                // Query was successful; print full record
                if( msgBuffer[0] == 8 ) {
                    fullRecord = (struct query_success*) msgBuffer;
                    print_record(fullRecord->record);
                }
                // Query was unsuccessful; print failure
                else {
                    printf("Record associated with %s not found\n", queryName);
                }
            }

        }

        else if( strcmp(token, "leave-dht") == 0) {     // LEAVE-DHT COMMAND ------------------------------
            struct leave_dht datagram;
            struct teardown teardown;
            struct reset_id reset;
            struct reset_left resetLeft;
            struct reset_right resetRight;
            struct rebuild_dht rebuild;
            struct dht_rebuilt rebuilt;
            char* username;
            char* new_leader;

            // Create datagram
            username = strtok(NULL, " ");
            datagram.command = 9;
            strcpy(datagram.user_name, username);
            datagram.ring_size = ring_size;

            // Send datagram to server
            if( sendto( sockServ, &datagram, sizeof(datagram), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(datagram) ) 
                DieWithError( "leave-dht: sendto() sent a different number of bytes than expected" );


            // Receive Success/Failure message
            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                DieWithError( "leave-dht: recvfrom() failed" );


            if( strcmp(msgBuffer, "FAILURE\n") == 0 ) {
                printf("%s", (char *) msgBuffer);
            }
            // If success is received, rebuild dht
            else {

                // Send teardown to right neighbor
                teardown.command = 10;
                if( sendto( sockSend, &teardown, sizeof(teardown), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(teardown) ) 
                    DieWithError( "teardown: sendto() sent a different number of bytes than expected" );

                // Receive teardown message
                if( ( recvfrom( sockRecv, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                    DieWithError( "teardown: recvfrom() failed" );
                if( msgBuffer[0] != 10 ) {
                    printf("Teardown error\n");
                    break;
                }

                // Delete local hash table
                delete_dht();

                // Send reset_id to right neighbor
                reset.command = 11;
                reset.id = 0;
                reset.ring_size = ring_size - 1;
                if( sendto( sockSend, &reset, sizeof(reset), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(reset) ) 
                    DieWithError( "reset_id: sendto() sent a different number of bytes than expected" );

                // Receive reset_id message
                if( ( recvfrom( sockRecv, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                    DieWithError( "reset_id: recvfrom() failed" );
                id = -1;
                ring_size = 0;

                // Send reset_left / reset_right
                resetLeft.command = 12;
                resetLeft.newAddr = toAddr;
                resetLeft.port = fromAddr.sin_port;     // Used to identify which process is the left neighbor
                resetRight.command = 13;
                resetRight.newAddr = fromAddr;

                if( sendto( sockSend, &resetLeft, sizeof(resetLeft), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(resetLeft) ) 
                    DieWithError( "reset_left: sendto() sent a different number of bytes than expected" );

                if( sendto( sockSend, &resetRight, sizeof(resetRight), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(resetRight) ) 
                    DieWithError( "reset_right: sendto() sent a different number of bytes than expected" );

                // Send rebuild-dht
                rebuild.command = 14;
                rebuild.addr = fromAddr;
                if( sendto( sockSend, &rebuild, sizeof(rebuild), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(rebuild) ) 
                    DieWithError( "rebuild_dht: sendto() sent a different number of bytes than expected" );
                
                // Receive username from new leader after dht is rebuilt
                if( ( recvfrom( sockRecv, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                    DieWithError( "reveive new_leader: recvfrom() failed" );
                new_leader = msgBuffer;

                //Send dht_rebuilt to server
                rebuilt.command = 15;
                rebuilt.FLAG = 0;
                strcpy(rebuilt.user_name, user_name);
                strcpy(rebuilt.new_leader, new_leader);
                if( sendto( sockServ, &rebuilt, sizeof(rebuilt), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(rebuilt) ) 
                    DieWithError( "dht_rebuilt: sendto() sent a different number of bytes than expected" );
            }
        }

        else if( strcmp(token, "join-dht") == 0) {      // JOIN-DHT COMMAND ------------------------------
            struct join_dht join;
            struct join_dht* response;
            struct dht_user leader;
            struct reset_left resetLeft;
            struct reset_right resetRight;
            struct teardown teardown;
            struct reset_id reset;
            struct dht_rebuilt rebuilt;
            char* username;

            // Create datagram
            username = strtok(NULL, " ");
            join.command = 16;
            strcpy(join.user_name, username);

            // Send datagram to server
            if( sendto( sockServ, &join, sizeof(join), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(join) ) 
                DieWithError( "join-dht: sendto() sent a different number of bytes than expected" );

            // Receive Success/Failure message. On success, receive the leader of the DHT
            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                DieWithError( "join-dht: recvfrom() failed" );


            if( strcmp(msgBuffer, "FAILURE\n") == 0 ) {
                printf("%s", (char *) msgBuffer);
            }
            // If success is received, rebuild dht
            else {
                // Set old leader as right neighbor
                response = (struct join_dht*) msgBuffer;
                leader = response->leader;

                memset( &toAddr, 0, sizeof( struct sockaddr_in ) );
                toAddr.sin_family = AF_INET;
                toAddr.sin_addr.s_addr = inet_addr( leader.ipAddr );
                toAddr.sin_port = htons( leader.portFrom );

                //Set ID and ring size. This process becomes the leader
                id = 0;
                ring_size = response->ring_size + 1;
                hashTable = calloc(353, sizeof(struct dht_entry*)); // Create space for hash table in memory

                // Send reset_left / reset_right
                resetLeft.command = 12;
                resetLeft.newAddr = fromAddr;
                resetLeft.port = htons( leader.portFrom );     // Used to identify which process is the left neighbor
                resetRight.command = 13;
                resetRight.newAddr = toAddr;

                if( sendto( sockSend, &resetLeft, sizeof(resetLeft), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(resetLeft) ) 
                    DieWithError( "reset_left: sendto() sent a different number of bytes than expected" );

                if( sendto( sockSend, &resetRight, sizeof(resetRight), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(resetRight) ) 
                    DieWithError( "reset_right: sendto() sent a different number of bytes than expected" );


                // Send teardown to old leader
                teardown.command = 10;
                if( sendto( sockSend, &teardown, sizeof(teardown), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(teardown) ) 
                    DieWithError( "teardown: sendto() sent a different number of bytes than expected" );

                // Receive teardown message
                if( ( recvfrom( sockRecv, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                    DieWithError( "teardown: recvfrom() failed" );
                if( msgBuffer[0] != 10 ) {
                    printf("Teardown error\n");
                    break;
                }

                // Send reset_id to old leader / new right neighbor
                reset.command = 11;
                reset.id = 1;
                reset.ring_size = ring_size;
                if( sendto( sockSend, &reset, sizeof(reset), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(reset) ) 
                    DieWithError( "reset_id: sendto() sent a different number of bytes than expected" );

                // Receive reset_id message
                if( ( recvfrom( sockRecv, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                    DieWithError( "reset_id: recvfrom() failed" );

                // Build the DHT
                populate_dht();

                //Send dht_rebuilt to server
                rebuilt.command = 15;
                rebuilt.FLAG = 1;
                strcpy(rebuilt.user_name, user_name);
                strcpy(rebuilt.new_leader, user_name);
                if( sendto( sockServ, &rebuilt, sizeof(rebuilt), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(rebuilt) ) 
                    DieWithError( "dht_rebuilt: sendto() sent a different number of bytes than expected" );
            }
        }

        else if( strcmp(token, "teardown-dht") == 0) {      // TEARDOWN-DHT COMMAND ------------------------------

            struct teardown_dht datagram;
            struct teardown teardown;
            struct teardown_complete complete;
            char* username;

            // Create datagram
            username = strtok(NULL, " ");
            datagram.command = 17;
            strcpy(datagram.user_name, username);

            // Send datagram to server
            if( sendto( sockServ, &datagram, sizeof(datagram), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(datagram) ) 
                DieWithError( "teardown-dht: sendto() sent a different number of bytes than expected" );

            // Receive Success/Failure message
            if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                DieWithError( "teardown-dht: recvfrom() failed" );


            if( strcmp(msgBuffer, "FAILURE\n") == 0 ) {
                printf("%s", (char *) msgBuffer);
            }
            // If success is received, teardown dht
            else {
                // Send teardown to right neighbor
                teardown.command = 10;
                if( sendto( sockSend, &teardown, sizeof(teardown), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(teardown) ) 
                    DieWithError( "teardown: sendto() sent a different number of bytes than expected" );

                // Receive teardown message
                if( ( recvfrom( sockRecv, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                    DieWithError( "teardown: recvfrom() failed" );
                if( msgBuffer[0] != 10 ) {
                    printf("Teardown error\n");
                    break;
                }

                // Teardown this node
                delete_dht();

                // Send teardown-complete
                complete.command = 18;
                strcpy(complete.user_name, username);
                if( sendto( sockServ, &complete, sizeof(complete), 0, (struct sockaddr *) &servAddr, sizeof( servAddr ) ) != sizeof(complete) ) 
                    DieWithError( "teardown-complete: sendto() sent a different number of bytes than expected" );

                // Receive Success/Failure message
                if( ( recvfrom( sockServ, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &recvAddr, &recvAddrLen )) < 0 ) 
                    DieWithError( "teardown-complete: recvfrom() failed" );
                printf("%s", (char *) msgBuffer);
            }
        }

        else if( strcmp( token, "test" ) == 0) {
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
        DieWithError( "Creation of socket failed" );

    memset( addr, 0, sizeof( struct sockaddr_in ) );           // Zero out structure
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

void setup_dht(struct dht_user* users, int n) {
    //Set ID of each process in ring
    for(int i = 0; i < n; i++){
        send_set_id( users[i], users[(i-1+n) % n], users[(i+1) % n], i, n);
    }

    //Populate the DHT
    populate_dht();
}

void send_set_id(struct dht_user user, struct dht_user left, struct dht_user right, int id, int n) {
    struct sockaddr_in addr;
    struct set_id mesg;

    // Create address structure of user_i
    memset( &addr, 0, sizeof( addr ) );
    addr.sin_family = AF_INET;              
    addr.sin_addr.s_addr = inet_addr( user.ipAddr ); 
    addr.sin_port = htons( user.portQuery ) ;     

    // Create message to send to user_i
    mesg.command = 3;
    mesg.id = id;
    mesg.ring_size = n;
    mesg.left = left;
    mesg.right = right;

    if(id == 0) set_id(&mesg);
    else {
        if( sendto( sockSend, &mesg, sizeof(mesg), 0, (struct sockaddr *) &addr, sizeof( addr ) ) != sizeof(mesg)  )
            DieWithError( "set_id: sendto() sent a different number of bytes than expected" );
    }
}

void set_id(struct set_id* info) {   
    printf("id:%d, rsize: %d, left: %s, right: %s\n", info->id, info->ring_size, info->left.user_name, info->right.user_name);

    //Set ID and ring size
    id = info->id;
    ring_size = info->ring_size;

    //Fill out sockaddr for the right neighbor, the peer that info will be SENT to
    memset( &toAddr, 0, sizeof( struct sockaddr_in ) );           
    toAddr.sin_family = AF_INET;                  
    toAddr.sin_addr.s_addr = inet_addr( info->right.ipAddr );     
    toAddr.sin_port = htons( info->right.portFrom );

    // Create space for hash table in memory
    hashTable = calloc(353, sizeof(struct dht_entry*));
}

void populate_dht() {
    char line[512];
    char* token;
    struct dht_entry* record;
    FILE* data = fopen("StatsCountry.csv", "r");
    if(data == NULL) printf("Failed to open file\n");

    // Parse record info and put into a struct dht_entry
    read_stats_line(line, data);            // Skip header line
    while( read_stats_line(line, data) ) {
        record = calloc(1, sizeof(struct dht_entry));

        token = get_token(line, ",");
        strcpy(record->countryCode, token);  // Country Code
        token = get_token(NULL, ",");
        strcpy(record->shortName, token);    // Short name
        token = get_token(NULL, ",");
        strcpy(record->tableName, token);    // Table Name
        token = get_token(NULL, ",");
        strcpy(record->longName, token);     // Long Name
        token = get_token(NULL, ",");
        strcpy(record->alphaCode, token);    // 2-Alpha Code
        token = get_token(NULL, ",");
        strcpy(record->currency, token);     // Currency Unit
        token = get_token(NULL, ",");
        strcpy(record->region, token);       // Region
        token = get_token(NULL, ",");
        strcpy(record->wbCode, token);       // WB-2 Code
        token = get_token(NULL, ",");
        strcpy(record->latestCensus, token); // Latest Population Cansus

        store(record);
    }
}

void store(struct dht_entry* record) {
    int pos = compute_record_pos(record->longName);
    int nodeID = pos % ring_size;
    struct store datagram;

    if(id == nodeID) {
        dht_insert(record, pos);
    }
    // Send record to next node in ring.
    else {
        datagram.command = 5;
        datagram.record = *record;

        if( sendto( sockSend, &datagram, sizeof(datagram), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(datagram) ) 
            DieWithError( "store: sendto() sent a different number of bytes than expected" );

        free(record);
    }
}

void dht_insert(struct dht_entry* record, int pos) {
    struct dht_entry* head = hashTable[pos];

    // Insert at head
    if(head == NULL) hashTable[pos] = record;
    // Insert into chain
    else {
        while(head->next != NULL) head = head->next;
    
        head->next = record;
    }

    //printf("Inserted: %s, %s\n", record->countryCode, record->longName);
}

int compute_record_pos(char* name) {
    int sum = 0;

    for(int i = 0; i < strlen(name); i++) {
        sum += name[i];
    }

    return sum % 353;
}

void print_record(struct dht_entry record) {
    printf("Country Code : %s\n", record.countryCode);
    printf("Short Name   : %s\n", record.shortName);
    printf("Table Name   : %s\n", record.tableName);
    printf("Long Name    : %s\n", record.longName);
    printf("2-Alpha Code : %s\n", record.alphaCode);
    printf("Currency Unit: %s\n", record.currency);
    printf("Region       : %s\n", record.region);
    printf("WB-2 Code    : %s\n", record.wbCode);
    printf("Latest Census: %s\n\n", record.latestCensus);
}

void process_query(struct query* query) {
    int pos = compute_record_pos(query->longName);
    int nodeId = pos % ring_size;
    struct dht_entry* record;
    struct query_success mesg;
    struct sockaddr_in addr = query->requesterAddr;

    // Record is in this node
    if(nodeId == id) {
        record = retrieve_record(query->longName, pos);

        // Record not found; return failure
        if(record == NULL) {
            if( sendto( sockQuery, "FAILURE\n\0", 9, 0, (struct sockaddr *) &addr, sizeof( addr ) ) != 9 )
                DieWithError( "query failure: sendto() sent a different number of bytes than expected" );    
        }
        // Send record to requester
        else {
            mesg.command = 8;
            mesg.record = copy_record(record);

            if( sendto( sockQuery, &mesg, sizeof(mesg), 0, (struct sockaddr *) &addr, sizeof( addr ) ) != sizeof(mesg) )
                DieWithError( "query success: sendto() sent a different number of bytes than expected" );    
        }
    }
    // Record is not in this node; continue to next node
    else {
        if( sendto( sockSend, query, sizeof(struct query), 0, (struct sockaddr *) &toAddr, sizeof( toAddr ) ) != sizeof(struct query) )
            DieWithError( "query: sendto() sent a different number of bytes than expected" );  
    }
}

struct dht_entry* retrieve_record(char* name, int pos) {
    struct dht_entry* node = hashTable[pos];

    while(1) {
        if(node == NULL) return NULL;
        if(strcmp(name, node->longName) == 0) return node;
        node = node->next;
    }

    return NULL;
}

struct dht_entry copy_record(struct dht_entry* record) {
    struct dht_entry copy;

    strcpy(copy.countryCode, record->countryCode);
    strcpy(copy.shortName, record->shortName);
    strcpy(copy.tableName, record->tableName);
    strcpy(copy.longName, record->longName);
    strcpy(copy.alphaCode, record->alphaCode);
    strcpy(copy.currency, record->currency);
    strcpy(copy.region, record->region);
    strcpy(copy.wbCode, record->wbCode);
    strcpy(copy.latestCensus, record->latestCensus);

    return copy;
}

void delete_dht() {     //Deletes local hash table
    for(int i = 0; i < 353; i++) {
        delete_dht_list(hashTable[i]);
    }

    free(hashTable);
}

void delete_dht_list(struct dht_entry* chain) {  // Deletes chain from hash table
    if(chain == NULL) return;
    delete_dht_list(chain->next);
    free(chain);
}


/*

./peer 10.120.70.145 29500

register i 10.120.70.106 29501 29502 29503

register j 10.120.70.145 29504 29505 29506

register k 10.120.70.106 29507 29508 29509

register l 10.120.70.106 29510 29511 29512

*/