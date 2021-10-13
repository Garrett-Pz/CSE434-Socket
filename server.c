#include "defn.h" 

// Declarations
struct user* user_register(struct user_register*, struct user*);
int check_user_unique(struct user*, struct user*);
int deregister( char*, struct user**);
struct user* find_user(char*, struct user*);
struct user* get_user(struct user*, int);
struct dht_user create_dht_user(struct user*);
int is_in(char* name, struct dht_user*, int);
struct user* create_rand_list( struct user*, int, struct dht_user*, int);
struct user* get_random_user(struct user*, int);

// Utility functions
void DieWithError( const char *errorMessage ) // External error handling function
{
    perror( errorMessage );
    exit( 1 );
}

void success(int sock, struct sockaddr_in clntAddr) {   //Returns success to client
    //char* s = "SUCCESS\n\0";

    if( sendto( sock, "SUCCESS\n\0", 9, 0, (struct sockaddr *) &clntAddr, sizeof( clntAddr ) ) != 9 )  // Send datagram to server
        DieWithError( "success: sendto() sent a different number of bytes than expected" );    
}

void failure(int sock, struct sockaddr_in clntAddr) {   //Returns failure to client
    //char* s = "FAILURE\n\0";

    if( sendto( sock, "FAILURE\n\0", 9, 0, (struct sockaddr *) &clntAddr, sizeof( clntAddr ) ) != 9 )  // Send datagram to server
        DieWithError( "failure: sendto() sent a different number of bytes than expected" );    
}

void get_line(char* buffer, int len, FILE* in) {    //Modification of fgets that removes trailing newlines
	fgets(buffer, len, in);
	char* newline = strchr(buffer, '\n');
	if (newline) *newline = '\0';
}


// Main method
int main( int argc, char *argv[] )
{
    int sock;                        // Socket
    struct sockaddr_in ServAddr;     // Local address of server
    struct sockaddr_in ClntAddr;     // Client address
    unsigned int cliAddrLen;         // Length of incoming message
    unsigned short ServPort;         // Server port
    char msgBuffer[ BUFFERMAX ];     // Buffer for received datagrams
    int dhtCreated = 0;              // 0: no dht, 1: dht is setup, 2: dht is being setup

    struct user* user_list = NULL;   // List of registered users
    int users = 0;                   // Size of user_list

    if( argc != 2 )         // Test for correct number of parameters
    {
        fprintf( stderr, "Usage:  %s <UDP SERVER PORT>\n", argv[ 0 ] );
        exit( 1 );
    }

    ServPort = atoi(argv[1]);  // First arg: local port

    // Create socket for sending/receiving datagrams
    if( ( sock = socket( PF_INET, SOCK_DGRAM, IPPROTO_UDP ) ) < 0 )
        DieWithError( "server: socket() failed" );

    // Construct local address structure */
    memset( &ServAddr, 0, sizeof( ServAddr ) ); // Zero out structure
    ServAddr.sin_family = AF_INET;                  // Internet address family
    ServAddr.sin_addr.s_addr = htonl( INADDR_ANY ); // Any incoming interface
    ServAddr.sin_port = htons( ServPort );      // Local port

    // Bind to the local address
    if( bind( sock, (struct sockaddr *) &ServAddr, sizeof(ServAddr)) < 0 )
        DieWithError( "server: bind() failed" );


    while(1) {
        cliAddrLen = sizeof( ClntAddr );

        // Block until receive command from a client
        if( ( recvfrom( sock, msgBuffer, BUFFERMAX, 0, (struct sockaddr *) &ClntAddr, &cliAddrLen )) < 0 )
            DieWithError( "server: recvfrom() failed" );

        
        
        if(dhtCreated == 2 && msgBuffer[0] != 4) failure(sock, ClntAddr);    //DHT is being established, send failure

        else if( msgBuffer[0] == 0 ) {  // CODE FOR REGISTER COMMAND -----------------------------------------

            struct user* new_user;
            struct user_register* datagram = (struct user_register*) msgBuffer;   //Extract data given by client

            if( (new_user = user_register( datagram, user_list)) != NULL ) {
                if(user_list == NULL) user_list = new_user;     // Add user to list
                else {
                    struct user* list_iterator = user_list;
                    while(list_iterator->next != NULL) list_iterator = list_iterator->next;
                    list_iterator->next = new_user;
                }       
                success(sock, ClntAddr);
                users++;
            }
            else    // User or port was already in list
                failure(sock, ClntAddr);

        }

        else if( msgBuffer[0] == 1 ) {  // CODE FOR DEREGISTER COMMAND -----------------------------------------

            struct deregister* datagram = (struct deregister *) msgBuffer;  //Extract data given by client

            if( deregister(datagram->user_name, &user_list) ) {  //Deregister
                success(sock, ClntAddr);
                users--;
            }
            else
                failure(sock, ClntAddr);

        }

        else if( msgBuffer[0] == 2 ) {  // CODE FOR SETUP-DHT COMMAND -----------------------------------------

            struct setup* datagram = (struct setup *) msgBuffer;            // Extract data given by client
            struct user* leader = find_user(datagram->user_name, user_list);     // Find requested dht leader

            //FAILURE CONDITIONS
            if( leader == NULL ) {   // User is not registered
                printf("Error: User not registered\n");
                failure(sock, ClntAddr);
            }
            else if( datagram->n < 2) {                   // n is too small
                printf("Error: DHT size must be larger\n");
                failure(sock, ClntAddr);
            }
            else if( users < datagram->n ) {              // Not enough registered users
                printf("Error: Not enough registered users\n");
                failure(sock, ClntAddr);
            }
            else if( dhtCreated == 1 ) {                  //DHT has already been created
                printf("Error: DHT has already been created\n");
                failure(sock, ClntAddr);
            }

            //NO FAILURE
            else{
                // Create list containing leader, and n-1 random users for dht construction
                leader->state = LEADER;
                int size = datagram->n * sizeof(struct dht_user);
                struct dht_user* dht_users = malloc( size );
                dht_users[0] = create_dht_user(leader);
                create_rand_list(user_list, (datagram->n)-1, dht_users + 1, users);

                //Send success
                success(sock, ClntAddr);
                
                // Send list to client
                if( sendto( sock, dht_users, size , 0, (struct sockaddr *) &ClntAddr, sizeof( ClntAddr ) ) != size )
       		        DieWithError( "setup-dht: sendto() sent a different number of bytes than expected" );
                
                printf("DHT Being Created\n");
                dhtCreated = 2;
                free(dht_users);
            }

        }

        else if( msgBuffer[0] == 4 ) {  // CODE FOR DHT-COMPLETE COMMAND -----------------------------------------

            struct dht_complete* datagram = (struct dht_complete*) msgBuffer;
            struct user* leader = find_user(datagram->user_name, user_list);

            if( leader == NULL ) failure(sock, ClntAddr);
            if( leader->state != LEADER) failure(sock, ClntAddr);

            printf("DHT Setup Complete\n");
            dhtCreated = 1;
            success(sock, ClntAddr);
        }

        else if ( msgBuffer[0] == 6 ) { // CODE FOR QUERY-DHT COMMAND -----------------------------------------

            struct query_dht* datagram = (struct query_dht*) msgBuffer;
            struct user* queryUser = find_user(datagram->user_name, user_list);
            struct user* randomUser;
            struct query_dht query;


            // Failure Conditions
            if( dhtCreated != 1 ) {
                printf("Error: DHT not created\n");
                failure(sock, ClntAddr);
            }
            else if( queryUser == NULL ) {
                printf("Error: User not registered\n");
                failure(sock, ClntAddr);
            }
            else if( queryUser->state != FREE ) {
                printf("Error: User is in DHT\n");
                failure(sock, ClntAddr);
            }
            // Success
            else {
                // Pick random user to be inital query node
                randomUser = get_random_user(user_list, users);

                // Send random user to client initiating query
                query.command = 6;
                strcpy(query.user_name, randomUser->user_name);
                strcpy(query.ipAddr, randomUser->ipAddr);
                query.portQuery = randomUser->portQuery;

                printf("Sent Random User\n");
                if( sendto( sock, &query, sizeof(query) , 0, (struct sockaddr *) &ClntAddr, sizeof( ClntAddr ) ) != sizeof(query) )
       		        DieWithError( "query-dht: sendto() sent a different number of bytes than expected" );
            }


        }

        else if( msgBuffer[0] == 120 ) {
            printf("Successful Test\n");

            struct sockaddr_in addr;

            memset( &addr, 0, sizeof( addr ) ); // Zero out structure
            addr.sin_family = AF_INET;                  // Internet address family
            addr.sin_addr.s_addr = inet_addr( "10.120.70.106" ); // Any incoming interface
            addr.sin_port = htons( 29501 );      // Local port

            char c = 100;
            if( sendto( sock, &c, 1, 0, (struct sockaddr *) &addr, sizeof( addr ) ) != 1 )
       		    DieWithError( "sendto() sent a different number of bytes than expected" );  
            //close(sock);
            //exit(0);
        }
    }
}

//Command Functions
struct user* user_register( struct user_register* user_info, struct user* user_list ) {
    struct user* new_user = malloc( sizeof( struct user ) );    //Create new user struct
    strcpy(new_user->user_name, user_info->user_name);          //Populate fields with info from received :18:
    strcpy(new_user->ipAddr, user_info->ipAddr);
    new_user->portFrom = user_info->portFrom;
    new_user->portTo = user_info->portTo;
    new_user->portQuery = user_info->portQuery;
    new_user->state = FREE;
    new_user->next = NULL;

    printf("Received User: %s\n", new_user->user_name);

    if( !check_user_unique( new_user, user_list )) {  // Check that user parameters are unique
        free(new_user);
        return NULL;
    }
    else {
        return new_user;
    }
}

int check_user_unique( struct user* u, struct user* list ) { 
    if( u->portFrom == u->portTo || u->portFrom == u->portQuery || u->portTo == u->portQuery ) return 0;
    while(list != NULL) {
        if( strcmp( u->user_name, list->user_name ) == 0) return 0;     //Check for matching username
        if( u->portFrom == list->portFrom  || u->portFrom == list->portTo || u->portFrom == list->portQuery) return 0;      //Check for any matching ports
        if( u->portTo == list->portFrom || u->portTo == list->portTo || u->portTo == list->portQuery ) return 0;
        if( u->portQuery == list->portFrom  || u->portQuery == list->portTo || u->portQuery == list->portQuery) return 0;
        list = list->next;
    }

    return 1;
}

int deregister( char* name, struct user** head) {
    struct user* ulist = *head;
    struct user* tmp;

    if( ulist == NULL ) return 0;                       // List is empty
    else if( strcmp(ulist->user_name, name) == 0 )  {    // User is the head
        *head = ulist->next;
        free(ulist);
        return 1;
    }
    else {
        while( ulist->next != NULL ){
            if( strcmp(ulist->next->user_name, name) == 0) {    // User is in the middle of the list or tail
                tmp = ulist->next;
                ulist->next = ulist->next->next;
                free(tmp);
                return 1;
            }
            ulist = ulist->next;
        }
    }

    return 0;   // User was not in list
}

struct user* find_user(char* name, struct user* list) { // Find a registered user with a given name
    while(list != NULL) {
        if( strcmp(name, list->user_name) == 0 ) return list;
        list = list->next;
    }

    return NULL;
}

struct user* get_user(struct user* list, int n){    // Reuturns user at index i of list of registered users
    if(list == NULL) return NULL;

  for(int i = 0; i < n; i++) {
        if (list->next == NULL) return NULL;
        list = list->next;
    }

    return list;
}

struct dht_user create_dht_user(struct user* u) {   // Given a registered user, create a dht_user struct
    struct dht_user user;

    strcpy(user.user_name, u->user_name);
    strcpy(user.ipAddr, u->ipAddr);
    user.portFrom = u->portFrom;
    user.portTo = u->portTo;
    user.portQuery = u->portQuery;

    return user;
}

int is_in(char* name, struct dht_user* list, int n) {   // Returns true if a given name exists in a dht_user list
    for(int i = 0; i < n; i++) {
        if( strcmp(name, list[i].user_name) == 0 ) return 1;
    }

    return 0;
}

struct user* create_rand_list( struct user* list, int n, struct dht_user* dht_users, int users ) {
    struct user* u;

    for(int i = 0; i < n; i++){
        int j = rand() % users;
        u = get_user(list, j);

        if(u->state == INDHT || u->state == LEADER) {
            i--;
            continue;
        }
        else {
            dht_users[i] = create_dht_user(u);
            u->state = INDHT;
        }
    }
    
}

struct user* get_random_user(struct user* list, int users) {
    struct user* u;

    while(1) {
        int j = rand() % users;
        u = get_user(list, j);
        if(u->state != FREE)
            break;
    }

    return u;
}
