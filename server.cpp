#include <cassert>
#include <cstring>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <sys/types.h>
#include <sys/stat.h>

#include <thread>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <vector>
#include <math.h>
#include <unistd.h>
#include "TCPRequestChannel.h"
using namespace std;

int buffercapacity = MAX_MESSAGE;
char *buffer = NULL; // buffer used by the server, allocated in the main

int nchannels = 0;
pthread_mutex_t newchannel_lock;
void handle_process_loop(TCPRequestChannel *_channel);
char ival;
vector<string> all_data[NUM_PERSONS];
vector<thread*> channel_threads;

// void process_newchannel_request (FIFORequestChannel *_channel){
// 	nchannels++;
// 	string new_channel_name = "data" + to_string(nchannels) + "_";
// 	char buf [30];
// 	strcpy (buf, new_channel_name.c_str());
// 	_channel->cwrite(buf, new_channel_name.size()+1);

// 	FIFORequestChannel *data_channel = new FIFORequestChannel (new_channel_name, FIFORequestChannel::SERVER_SIDE);
// 	channel_threads.push_back(thread (handle_process_loop, data_channel));

// 	//cout << "Channel " << new_channel_name << " made\n";
// }

void populate_file_data(int person)
{
	//cout << "populating for person " << person << endl;
	string filename = "BIMDC/" + to_string(person) + ".csv";
	char line[100];
	ifstream ifs(filename.c_str());
	if (ifs.fail())
	{
		EXITONERROR("Data file: " + filename + " does not exist in the BIMDC/ directory");
	}
	int count = 0;
	while (!ifs.eof())
	{
		line[0] = 0;
		ifs.getline(line, 100);
		if (ifs.eof())
			break;
		double seconds = stod(split(string(line), ',')[0]);
		if (line[0])
			all_data[person - 1].push_back(string(line));
	}
}

double get_data_from_memory(int person, double seconds, int ecgno)
{
	int index = (int)round(seconds / 0.004);
	string line = all_data[person - 1][index];
	vector<string> parts = split(line, ',');
	double sec = stod(parts[0]);
	double ecg1 = stod(parts[1]);
	double ecg2 = stod(parts[2]);
	if (ecgno == 1)
		return ecg1;
	else
		return ecg2;
}

void process_file_request(TCPRequestChannel *rc, Request *request)
{

	FileRequest f = *(FileRequest *)request;
	string filename = (char *)request + sizeof(FileRequest);
	if (filename.empty())
	{
		Request r(UNKNOWN_REQ_TYPE);
		rc->cwrite(&r, sizeof(r));
		return;
	}

	filename = "BIMDC/" + filename;
	int fd = open(filename.c_str(), O_RDONLY);
	if (fd < 0)
	{
		cerr << "Server received request for file: " << filename << " which cannot be opened" << endl;
		Request r(UNKNOWN_REQ_TYPE);
		rc->cwrite(&r, sizeof(r));
		return;
	}

	if (f.offset == 0 && f.length == 0)
	{ // means that the client is asking for file size
		int64 fs = lseek(fd, 0, SEEK_END);
		rc->cwrite((char *)&fs, sizeof(int64));
		close(fd);
		return;
	}

	/* request buffer can be used for response buffer, because everything necessary have
	been copied over to filemsg f and filename*/
	char *response = (char *)request;

	// make sure that client is not requesting too big a chunk
	if (f.length > buffercapacity)
	{
		cerr << "Client is requesting a chunk bigger than server's capacity" << endl;
		Request r(UNKNOWN_REQ_TYPE);
		rc->cwrite(&r, sizeof(r));
		return;
	}

	lseek(fd, f.offset, SEEK_SET);
	int nbytes = read(fd, response, f.length);
	/* making sure that the client is asking for the right # of bytes,
	this is especially imp for the last chunk of a file when the 
	remaining lenght is < buffercap of the client*/
	if (nbytes != f.length)
	{
		cerr << "The server received an incorrect length in the filemsg field" << endl;
		Request r(UNKNOWN_REQ_TYPE);
		rc->cwrite(&r, sizeof(r));
		close(fd);
		return;
	}
	int numBytesWrote = rc->cwrite(response, nbytes);
	//std::cout << "Wrote " << numBytesWrote << " bytes to sockfd " << rc->getfd() << "\n";
	close(fd);
}

void process_data_request(TCPRequestChannel *rc, Request *r)
{
	DataRequest *d = (DataRequest *)r;

	if (d->person < 1 || d->person > 15 || d->seconds < 0 || d->seconds >= 60.0 || d->ecgno < 1 || d->ecgno > 2)
	{
		cerr << "Incorrectly formatted data request" << endl;
		Request resp(UNKNOWN_REQ_TYPE);
		rc->cwrite(&resp, sizeof(Request));
		return;
	}
	double data = get_data_from_memory(d->person, d->seconds, d->ecgno);
	rc->cwrite(&data, sizeof(double));
}

void process_unknown_request(TCPRequestChannel *rc)
{
	Request resp(UNKNOWN_REQ_TYPE);
	rc->cwrite(&resp, sizeof(Request));
}

void process_request(TCPRequestChannel *rc, Request *r)
{
	if (r->getType() == DATA_REQ_TYPE)
	{
		usleep(rand() % 5000);
		process_data_request(rc, r);
	}
	else if (r->getType() == FILE_REQ_TYPE)
	{
		process_file_request(rc, r);
	}
	//else if (r->getType() == NEWCHAN_REQ_TYPE)
	//{
	// 	process_newchannel_request(rc);
	//}
	else
	{
		process_unknown_request(rc);
	}
}

void handle_process_loop(TCPRequestChannel *channel)
{
	/* creating a buffer per client to process incoming requests
	and prepare a response */
	char *buffer = new char[buffercapacity];
	if (!buffer)
	{
		EXITONERROR("Cannot allocate memory for server buffer");
	}
	while (true)
	{
		//std::cout << "About to read from sockfd " << channel->getfd() << "\n";
		int nbytes = channel->cread(buffer, buffercapacity);
		//std::cout << "Read " << nbytes << " bytes from sockfd " << channel->getfd() << "\n";
		if (nbytes < 0)
		{
			cerr << "Client-side terminated abnormally" << endl;
			break;
		}
		else if (nbytes == 0)
		{
			cerr << "ERROR: Server could not read anything... closing connection" << endl;
			break;
		}
		Request *r = (Request *)buffer;
		if (r->getType() == QUIT_REQ_TYPE)
		{
			//cout << "Connection with " << channel->getfd() << " closed by client" << endl;
            //system("clear");
            //cout << --nchannels << " connections\n";
			break;
			// note that QUIT_MSG does not get a reply from the server
		}
		process_request(channel, r);
	}
	delete[] buffer;
	delete channel;
}

int main(int argc, char *argv[])
{
	buffercapacity = MAX_MESSAGE;
	int opt;
	std::string r = "25565";
	while ((opt = getopt(argc, argv, "m:r:")) != -1)
	{
		switch (opt)
		{
		case 'm':
			buffercapacity = atoi(optarg);
			break;
		case 'r':
			r = std::string(optarg);
			break;
		}
	}
	srand(time_t(NULL));
	for (int i = 0; i < NUM_PERSONS; i++)
	{
		populate_file_data(i + 1);
	}

	int sockfd, new_fd; // listen on sock_fd, new connection on new_fd
	struct addrinfo hints, *serv;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	char s[INET6_ADDRSTRLEN];
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, r.c_str(), &hints, &serv)) != 0)
	{
		cerr << "getaddrinfo: " << gai_strerror(rv) << endl;
		return -1;
	}
	if ((sockfd = socket(serv->ai_family, serv->ai_socktype, serv->ai_protocol)) == -1)
	{
		perror("server: socket");
		return -1;
	}
	if (bind(sockfd, serv->ai_addr, serv->ai_addrlen) == -1)
	{
		close(sockfd);
		perror("server: bind");
		return -1;
	}
	freeaddrinfo(serv); // all done with this structure

	if (listen(sockfd, 20) == -1)
	{
		perror("listen");
		exit(1);
	}

	cout << "server: waiting for connections..." << endl;
	char buf[1024];
	while (1)
	{ // main accept() loop
		sin_size = sizeof their_addr;
		int client_socket = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (client_socket == -1)
		{
			perror("accept");
			continue;
		}
		TCPRequestChannel *control_channel = new TCPRequestChannel(client_socket);
		thread *T = new thread(handle_process_loop, control_channel);
		channel_threads.push_back(T);
		T->detach();
		//cout << "Connection with " << control_channel->getfd() << " opened by client" << endl;
        //system("clear");
        //cout << ++nchannels << " connections\n";
	}

	//handle_process_loop(control_channel);
	for (int i = 0; i < channel_threads.size(); i++)
	{
		std::cout << i << "\n";
		channel_threads[i]->join();
	}
	cout << "Server process exited" << endl;
}
