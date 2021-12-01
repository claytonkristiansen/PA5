#include "TCPRequestChannel.h"

void connection_handler (int client_socket){
      
    char buf [1024];
    while (true){
        if (recv (client_socket, buf, sizeof (buf), 0) < 0){
            perror ("server: Receive failure");    
            exit (0);
        }
        int num = *(int *)buf;
        num *= 2;
        if (num == 0)
            break;
        if (send(client_socket, &num, sizeof (num), 0) == -1){
            perror("send");
            break;
        }
    }
    cout << "Closing client socket" << endl;
	close(client_socket);
}

/* Constructor takes 2 arguments: hostname and port not
     If the host name is an empty string, set up the channel for
      the server side. If the name is non-empty, the constructor
     works for the client side. Both constructors prepare the
     sockfd  in the respective way so that it can works as a    server or client communication endpoint*/
TCPRequestChannel::TCPRequestChannel(const std::string host_name, const std::string port_no)
{
    if(host_name != "")
    {
        struct addrinfo hints, *res;

        // first, load up address structs with getaddrinfo():
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        int status;
        //getaddrinfo("www.example.com", "3490", &hints, &res);
        if ((status = getaddrinfo(host_name.c_str(), port_no.c_str(), &hints, &res)) != 0) {
            cerr << "getaddrinfo: " << gai_strerror(status) << endl;
        }

        // make a socket:
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd < 0){
            perror ("Cannot create socket");
        }

        // connect!
        if (connect(sockfd, res->ai_addr, res->ai_addrlen)<0){
            perror ("Cannot Connect");
        }
        //
        //cout << "Connected " << endl;
        // now it is time to free the memory dynamically allocated onto the "res" pointer by the getaddrinfo function
        freeaddrinfo (res);
    }
    else
    {
       	int new_fd;  // listen on sock_fd, new connection on new_fd
        struct addrinfo hints, *serv;
        struct sockaddr_storage their_addr; // connector's address information
        socklen_t sin_size;
        char s[INET6_ADDRSTRLEN];
        int rv;

        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE; // use my IP

        if ((rv = getaddrinfo(NULL, port_no.c_str(), &hints, &serv)) != 0) {
            cerr  << "getaddrinfo: " << gai_strerror(rv) << endl;
        }
        if ((sockfd = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol)) == -1) {
            perror("server: socket");
        }
        if (bind(sockfd, serv->ai_addr, serv->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
        }
        freeaddrinfo(serv); // all done with this structure

        if (listen(sockfd, 20) == -1) {
            perror("listen");
            exit(1);
        }
        
        cout << "server: waiting for connections..." << endl;
        char buf [1024];
        while(1) {  // main accept() loop
            sin_size = sizeof their_addr;
            int client_socket = accept (sockfd, (struct sockaddr *)&their_addr, &sin_size);
            if (client_socket == -1) {
                perror("accept");
                continue;
            }
            thread t (connection_handler, client_socket);
            t.detach (); 
        }
    }
}

TCPRequestChannel::TCPRequestChannel(int socket)
{
    sockfd = socket;
}

TCPRequestChannel::~TCPRequestChannel()
{
    close(sockfd);
}

int TCPRequestChannel::cread (void* msgbuf, int buflen)
{
    return recv(sockfd, msgbuf, buflen, 0);
}

int TCPRequestChannel::cwrite(void* msgbuf, int msglen)
{
    return send(sockfd, msgbuf, msglen, 0);
}

int TCPRequestChannel::getfd()
{
    return sockfd;
}