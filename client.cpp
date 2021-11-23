#include "common.h"
#include "FIFOreqchannel.h"
#include "BoundedBuffer.h"
#include "HistogramCollection.h"
#include <sys/wait.h>
#include <thread>
#include <signal.h>
using namespace std;

enum THING_REQUEST_TYPE {FILE_REQUEST_TYPE, DATA_REQUEST_TYPE};

HistogramCollection hc;
bool programDone;
mutex mut;

struct Response
{
	int m_patient;
	double m_ecg;
	bool m_kill = false;
	Response(int patient, double ecg)
	{
		m_patient = patient;
		m_ecg = ecg;
	}
	Response(bool kill)
	{
		m_kill = kill;
	}
	Response()
	{
		m_patient = 0;
		m_ecg = 0;
	}
};

vector<char> CharArrToVecOfChar(char *arr, int size)
{
	vector<char> vec;
	for(int i = 0; i < size; ++i)
	{
		vec.push_back(arr[i]);
	}
	return vec;
}

vector<char> ResponseToVecOfChar(Response res)
{
	char buf[sizeof(Response)];
	std::memcpy(buf, &res, sizeof(Response));
	return CharArrToVecOfChar(buf, sizeof(Response));
}

char* VecOfCharToCharArr(vector<char> vec)
{
	char *arr = new char[vec.size()];
	for(int i = 0; i < vec.size(); ++i)
	{
		arr[i] = vec[i];
	}
	return arr;
}

Response VecOfCharToResponse(vector<char> vec)
{
	char* buf = VecOfCharToCharArr(vec);
	Response r;
	std::memcpy(&r, buf, vec.size());
	delete[] buf;
	return r;
}

void BonusSignalHander(int sigNumber)
{
	if(!programDone)
	{
		system("clear");
		mut.lock();
		hc.print();
		mut.unlock();
		alarm(2);
	}
	return;
}

void patient_thread_function(THING_REQUEST_TYPE rqType, BoundedBuffer *requestBuffer, int patient, int numItems, string fileName)
{
	switch(rqType)
	{
		case DATA_REQUEST_TYPE:
			for(int i = 0; i < numItems; ++i)
			{
				char buf[sizeof(DataRequest)];
				DataRequest dataRequest(patient, 0.004 * i, 1);
				std::memcpy(buf, &dataRequest, sizeof(DataRequest));
				requestBuffer->push(CharArrToVecOfChar(buf, sizeof(DataRequest)));
				// dataRequest = DataRequest(patient, 0.004 * i, 2);
				// std::memcpy(buf, &dataRequest, sizeof(DataRequest));
				// requestBuffer->push(CharArrToVecOfChar(buf, sizeof(DataRequest)));
			}
			break;
		case FILE_REQUEST_TYPE:
			
			break;
	}
}

void worker_thread_function(THING_REQUEST_TYPE rqType, FIFORequestChannel *chan, Semaphore *fileMutex, BoundedBuffer *requestBuffer, BoundedBuffer *responseBuffer, std::ofstream *fout, int bufferCapacity)
{
	// Request q(QUIT_REQ_TYPE);
	// chan->cwrite(&q, sizeof(Request));
	if(rqType == DATA_REQUEST_TYPE)
	{
		bool done = false;
		while(!done)
		{
			vector<char> req = requestBuffer->pop();
			int reqBufSize = req.size();
			char *reqBuf = VecOfCharToCharArr(req);				//Memory is allocated on heap here
			Request quitRequest(QUIT_REQ_TYPE);
			if(std::memcmp(reqBuf, &quitRequest, sizeof(Request)) == 0)	//break
			{
				chan->cwrite(&quitRequest, sizeof(Request));
				delete[] reqBuf;
				break;
			}
			DataRequest dataReq(0, 0, 0);
			std::memcpy(&dataReq, reqBuf, sizeof(DataRequest));	//Copy memory into DataRequest object
			int patient = dataReq.person;						//Extract the patient number
 
			chan->cwrite (&dataReq, sizeof(DataRequest)); //question
			double reply;
			chan->cread (&reply, sizeof(double)); //answer
			// cout << "s\n";
			responseBuffer->push(ResponseToVecOfChar(Response(patient, reply)));
			// cout << "f\n";
			//cout << patient << ": " << reply << "\n";
			delete[] reqBuf;
		}
	}
	else if(rqType == FILE_REQUEST_TYPE)
	{
		bool done = false;
		while(!done)
		{
			vector<char> req = requestBuffer->pop();
			int reqBufSize = req.size();
			char *reqBuf = VecOfCharToCharArr(req);					//Memory is allocated on heap here
			Request quitRequest(QUIT_REQ_TYPE);
			if(std::memcmp(reqBuf, &quitRequest, sizeof(Request)) == 0)	//break
			{
				chan->cwrite(&quitRequest, sizeof(Request));
				delete[] reqBuf;
				break;
			}
			FileRequest fileReq(0, 0);
			std::memcpy(&fileReq, reqBuf, sizeof(FileRequest));	//Copy memory into FileRequest object
			
			if(fileReq.length == 219)
			{
				int k = 0;
			}

			chan->cwrite(reqBuf, reqBufSize);
			char buf4[bufferCapacity];
			chan->cread(buf4, bufferCapacity);	//Read data from FIFO
			fileMutex->P();
			fout->seekp(fileReq.offset);
			fout->write(buf4, fileReq.length);		//Write data to file
			fileMutex->V();
			delete[] reqBuf;
		}
		fout->close();
	}
	// cout << "WORKER THREAD STOPPED\n";
}
void histogram_thread_function(HistogramCollection *histogramCollection, BoundedBuffer *responseBuffer)
{
	bool done = false;
	while(!done)
	{
		// cout << "s\n";
		vector<char> responseVec = responseBuffer->pop();
		// cout << "f\n";
		Response response = VecOfCharToResponse(responseVec);
		if(response.m_kill)	//break
		{
			break;
		}
		histogramCollection->update(response.m_patient, response.m_ecg);
	}
	// cout << "HISTOGRAM THREAD STOPPED\n";
}

void file_request_thread_function(BoundedBuffer* requestBuffer, int fileLen, string fileName, int bufferCapacity)
{
	std::cout << "File length is: " << fileLen << " bytes" << endl;
	ofstream file2("received/" + fileName);
	int len = sizeof (FileRequest) + fileName.size()+1;
	for(int byteOffset = 0; byteOffset < fileLen; byteOffset += bufferCapacity)
	{
		int requestAmount = bufferCapacity;
		if(byteOffset + bufferCapacity > fileLen) //If this is the last request adjust requestAmount
		{
			requestAmount = fileLen - byteOffset;
		}
		char buf3[len];
		FileRequest fq(byteOffset, requestAmount);
		std::memcpy (buf3, &fq, sizeof (FileRequest));
		std::strcpy (buf3 + sizeof (FileRequest), fileName.c_str());
		requestBuffer->push(CharArrToVecOfChar(buf3, len));
	}
}
int main(int argc, char *argv[])
{
	programDone = false;
	int opt;
	int p = 1;
	double t = 0.0;
	int e = 1;
	string filename = "";
	int b = 10; // size of bounded buffer, note: this is different from another variable buffercapacity/m
	// take all the arguments first because some of these may go to the server

	int n = 1000;
	int w = 50;
	int h = 5;
	int m = 256;
	THING_REQUEST_TYPE reqType = DATA_REQUEST_TYPE;
	Semaphore fileMutex(1);

	while ((opt = getopt(argc, argv, "f:n:p:w:b:h:m:")) != -1)
	{
		switch (opt)
		{
		case 'f':
			filename = string(optarg);
			reqType = FILE_REQUEST_TYPE;
			break;
		case 'n':
			n = stoi(optarg);
			break;
		case 'p':
			p = stoi(optarg);
			break;
		case 'w':
			w = stoi(optarg);
			break;
		case 'b':
			b = stoi(optarg);
			break;
		case 'h':
			h = stoi(optarg);
			break;
		case 'm':
			m = stoi(optarg);
			break;
		}
	}

	int pid = fork();
	if (pid < 0)
	{
		EXITONERROR("Could not create a child process for running the server");
	}
	if (!pid)
	{ // The server runs in the child process
		std::string mString = to_string(m);
		char mStr[mString.length() + 1];
		strcpy(mStr, mString.c_str());
		char *args[] = {"./server", "-m", mStr, nullptr};
		if (execvp(args[0], args) < 0)
		{
			EXITONERROR("Could not launch the server");
		}
		return 0;
	}
	FIFORequestChannel chan("control", FIFORequestChannel::CLIENT_SIDE);
	BoundedBuffer request_buffer(b);
	BoundedBuffer response_buffer(b);

	struct timeval start, end;
	gettimeofday(&start, 0);

	vector<FIFORequestChannel*> channels;

	/* Start all threads here */
	vector<std::thread*> patientThreads;
	vector<std::thread*> workerThreads;
	vector<std::thread*> histogramThreads;

	std::ofstream *fout = nullptr;
	if(reqType == FILE_REQUEST_TYPE)
	{
		fout = new std::ofstream("Recieved/" + filename);
	}

	for(int i = 0; i < w; ++i)
	{
		Request nc (NEWCHAN_REQ_TYPE);
		chan.cwrite (&nc, sizeof(nc));
		char nameBuf[b];
		chan.cread(nameBuf, b);
		FIFORequestChannel *newChan = new FIFORequestChannel(string(nameBuf), FIFORequestChannel::CLIENT_SIDE);		//Request new channel
		channels.push_back(newChan);
		std::thread *workerThread = new std::thread(worker_thread_function, reqType, newChan, &fileMutex, &request_buffer, &response_buffer, fout, m);
		workerThreads.push_back(workerThread);
	}

	switch(reqType)
	{
		case DATA_REQUEST_TYPE:
			alarm(2);
			signal(SIGALRM, BonusSignalHander);
			for(int i = 1; i <= p; ++i)			//Create Patient threads
			{
				std::thread *patientThread = new std::thread(patient_thread_function, reqType, &request_buffer, i, n, filename);
				patientThreads.push_back(patientThread);

				Histogram *h = new Histogram(10, -2, 2);
				hc.add(h);
			}	
			for(int i = 0; i < h; ++i)			//Create Histogram threads
			{
				std::thread *histogramThread = new std::thread(histogram_thread_function, &hc, &response_buffer);
				histogramThreads.push_back(histogramThread);
			}
			for(std::thread* t : patientThreads)
			{
				t->join();
			}
			for(int i = 0; i < w; ++i)
			{
				Request q (QUIT_REQ_TYPE);
				char *buf = new char[sizeof(Request)];
				std::memcpy(buf, &q, sizeof(Request));
				request_buffer.push(CharArrToVecOfChar(buf, sizeof(Request)));
				delete[] buf;
			}
			for(std::thread* t : workerThreads)
			{
				t->join();
			}
			for(int i = 0; i < h; ++i)
			{
				Response r(true);
				char buf[sizeof(Response)];
				std::memcpy(buf, &r, sizeof(Response));
				response_buffer.push(CharArrToVecOfChar(buf, sizeof(Response)));
			}
			for(std::thread* t : histogramThreads)
			{
				t->join();
			}
			break;
		case FILE_REQUEST_TYPE:
			int64 filelen;	
			FileRequest fm (0,0);
			int len = sizeof (FileRequest) + filename.size()+1;
			char buf2 [len];
			std::memcpy (buf2, &fm, sizeof (FileRequest));
			std::strcpy (buf2 + sizeof (FileRequest), filename.c_str());
			chan.cwrite (buf2, len);  
			chan.cread (&filelen, sizeof(int64));
			if (isValidResponse(&filelen))
			{
				std::thread fileReqestThread(file_request_thread_function, &request_buffer, filelen, filename, m);
				fileReqestThread.join();
			}
			for(int i = 0; i < w; ++i)
			{
				Request q (QUIT_REQ_TYPE);
				char *buf = new char[sizeof(Request)];
				std::memcpy(buf, &q, sizeof(Request));
				request_buffer.push(CharArrToVecOfChar(buf, sizeof(Request)));
				delete[] buf;
			}
			for(std::thread* t : workerThreads)
			{
				t->join();
			}
			break;
	}
	
	//Taking care of memory business
	delete fout;
	for(FIFORequestChannel *chanP : channels)
	{
		delete chanP;
	}
	for(std::thread *t : patientThreads)
	{
		delete t;
	}
	for(std::thread *t : workerThreads)
	{
		delete t;
	}
	for(std::thread *t : histogramThreads)
	{
		delete t;
	}

	/* Join all threads here */
	gettimeofday(&end, 0);

	// print the results and time difference
	hc.print();
	int secs = (end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec) / (int)1e6;
	int usecs = (int)(end.tv_sec * 1e6 + end.tv_usec - start.tv_sec * 1e6 - start.tv_usec) % ((int)1e6);
	std::cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

	ofstream dataFOut("data.csv", ios::out | ios::app);
	dataFOut << secs << "." << usecs << "\n";

	// closing the channel
	Request q(QUIT_REQ_TYPE);
	chan.cwrite(&q, sizeof(Request));
	// client waiting for the server process, which is the child, to terminate
	wait(0);
	std::cout << "Client process exited" << endl;
	programDone = true;
}
