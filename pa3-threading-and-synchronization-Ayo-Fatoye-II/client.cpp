#include <fstream>
#include <iostream>
#include <thread>
#include <sys/time.h>
#include <sys/wait.h>

#include "BoundedBuffer.h"
#include "common.h"
#include "Histogram.h"
#include "HistogramCollection.h"
#include "FIFORequestChannel.h"
#define EGCNO 1

using namespace std;

void patient_thread_function(BoundedBuffer *buffer, int a, int m)
{

    float timing = 0;

    for (int i = 0; i < m; i++)
    {
        timing = .004 * i;
        datamsg x(a, timing, EGCNO);
        buffer->push((char *)&x, sizeof(x));
    }
}

int64_t get_file_length(string currFile, FIFORequestChannel *currChannel)
{
    filemsg currMsg(0, 0);
    string _thisFile = currFile;
    // int msgLength = sizeof(filemsg) + (_thisFile.size() + 1);
    char *buf = new char[(sizeof(filemsg) + (_thisFile.size() + 1))];
    memcpy(buf, &currMsg, sizeof(filemsg));
    strcpy(buf + sizeof(filemsg), _thisFile.c_str());
    currChannel->cwrite(buf, (sizeof(filemsg) + (_thisFile.size() + 1)));
    delete[] buf;
    int64_t _sz = 0;
    currChannel->cread(&_sz, sizeof(size_t));
    return _sz;
}

void file_thread_function(BoundedBuffer *incoming, string currFile, int yNum, int64_t _sz)
{

    filemsg fm(0, 0);
    string fname = currFile;
    char *buf = new char[(sizeof(filemsg) + (fname.size() + 1))];
    filemsg yMsg(0, 0);
    memcpy(buf, &yMsg, sizeof(yMsg));
    memcpy(buf + sizeof(filemsg), fname.c_str(), fname.length() + 1);
    FILE *CF;
    currFile = "received/" + currFile;
    CF = fopen((const char *)currFile.c_str(), "wb");
    if (CF == nullptr)
    {
        return;
    }
    fseek(CF, _sz, SEEK_SET);
    fclose(CF);

    int offset = 0;

    while (offset < _sz)
    {
        if (_sz - offset < yNum)
        {
            yNum = _sz - offset;
        }

        ((filemsg *)buf)->offset = offset;
        ((filemsg *)buf)->length = yNum;

        incoming->push(buf, (sizeof(filemsg) + (fname.size() + 1)));

        offset += yNum;
    }

    incoming->push((char *)NULL, 0);

    delete[] buf;
}

void worker_thread_function(BoundedBuffer *incoming, BoundedBuffer *outgoing, FIFORequestChannel *frc, int yNum, bool requested)
{

    if (requested == false)
    {

        datamsg msg(1, 2, 3);
        for (;;)
        {
            int length = incoming->pop((char *)&msg, sizeof(msg));

            if (!length)
            {
                incoming->push((char *)NULL, 0);
                break;
            }
            frc->cwrite(&msg, length);

            double ecg;
            frc->cread(&ecg, sizeof(ecg));

            pair<int, double> person(msg.person, ecg);
            outgoing->push((char *)&person, sizeof(person));
        }
    }
    else
    {

        char *buffer1 = new char[yNum];
        filemsg *msg = (filemsg *)buffer1;

        for (;;)
        {

            int length = incoming->pop(buffer1, yNum);

            if (!length)
            {
                incoming->push((char *)NULL, 0);
                break;
            }

            frc->cwrite(buffer1, yNum);
            char *buffer2 = new char[yNum];
            frc->cread(buffer2, msg->length);

            if (!length)
            {

                incoming->push((char *)NULL, 0);

                break;
            }

            string currFile = (buffer1 + sizeof(filemsg));

            currFile = "received/" + currFile;

            FILE *CF;
            CF = fopen((const char *)currFile.c_str(), "rb+");
            if (CF == NULL)
            {
                return;
            }

            fseek(CF, msg->offset, SEEK_SET);

            fwrite(buffer2, 1, msg->length, CF);

            fclose(CF);

            delete[] buffer2;
        }
        delete[] buffer1;
    }
}

void histogram_thread_function(BoundedBuffer *incoming, HistogramCollection *hist)
{
    for (;;)
    {
        pair<int, double> person(-5, 4);
        int msg_length = incoming->pop((char *)&person, sizeof(person));

        if (!msg_length)
        {
            incoming->push((char *)NULL, 0); 
            break;
        }

        hist->update(person.first, person.second);
    }
}

int main(int argc, char *argv[])
{
    int n = 1000;        
    int p = 10;          
    int w = 100;         
    int h = 20;          
    int b = 20;          
    int m = MAX_MESSAGE;
    string f = "";       
    bool requested = false;

    // read arguments
    int opt;
    while ((opt = getopt(argc, argv, "n:p:w:h:b:m:f:")) != -1)
    {
        switch (opt)
        {
        case 'n':
            n = atoi(optarg);
            break;
        case 'p':
            p = atoi(optarg);
            break;
        case 'w':
            w = atoi(optarg);
            break;
        case 'h':
            h = atoi(optarg);
            break;
        case 'b':
            b = atoi(optarg);
            break;
        case 'm':
            m = atoi(optarg);
            break;
        case 'f':
            f = optarg;
            requested = true;
            break;
        }
    }

    // fork and exec the server
    int pid = fork();
    if (pid == 0)
    {
        execl("./server", "./server", "-m", (char *)to_string(m).c_str(), nullptr);
    }

    // initialize overhead (including the control channel)
    FIFORequestChannel *chan = new FIFORequestChannel("control", FIFORequestChannel::CLIENT_SIDE);
    BoundedBuffer outgoing(b);
    BoundedBuffer incoming(b);
    HistogramCollection histColl;

    // vector creation for thread vectors

    vector<thread> patients;
    vector<FIFORequestChannel *> fChans;
    vector<thread> dub;
    vector<thread> histThread;

    // making histograms and adding to collection
    for (int i = 0; i < p; i++)
    {
        Histogram *h = new Histogram(10, -2.0, 2.0);
        histColl.add(h);
    }

    // record start time
    struct timeval start, end;
    gettimeofday(&start, 0);

    /* create all threads here */

    if (!requested)
    {
        for (int i = 1; i <= p; i++)
        {
            patients.push_back(thread(patient_thread_function, &incoming, i, n)); 
        }
        for (int i = 0; i < h; i++)
        {
            histThread.push_back(thread(histogram_thread_function, &outgoing, &histColl)); 
        }
    }
    else
    {
        int64_t size = get_file_length(f, chan);
        patients.push_back(thread(file_thread_function, &incoming, f, m, size)); 
    }

    // making worker threads and channels

    for (int i = 0; i < w; i++)
    {

        MESSAGE_TYPE newmsg = NEWCHANNEL_MSG;
        chan->cwrite(&newmsg, sizeof(newmsg));
        char *newChannels = new char[256];
        chan->cread(newChannels, 256);
        fChans.push_back(new FIFORequestChannel(newChannels, FIFORequestChannel::CLIENT_SIDE));
        dub.push_back(thread(worker_thread_function, &incoming, &outgoing, fChans.at(i), m, requested)); // fix after worker thread function is done
        delete[] newChannels;
    }

    /* join all threads here */

    // ORDER CAN CHANGE, THIS IS PRELIMINARY

    if (requested == false)
    {
        for (unsigned long int i = 0; i < patients.size(); i++)
        {
            patients.at(i).join();
        }

        incoming.push((char *)NULL, 0); 

        for (unsigned long int i = 0; i < dub.size(); i++)
        {
            dub.at(i).join();
        }

        outgoing.push((char *)NULL, 0); 

        for (unsigned long int i = 0; i < histThread.size(); i++)
        {
            histThread.at(i).join();
        }
    }
    else
    {

        patients.at(0).join();
        // request_buffer.push((char*) NULL, 0);

        for (unsigned long int i = 0; i < dub.size(); i++)
        {
            dub.at(i).join();
        }
    }

   

    // record end time
    gettimeofday(&end, 0);

    // print the results
    if (f == "")
    {
        histColl.print();
    }
    int secs = ((1e6 * end.tv_sec - 1e6 * start.tv_sec) + (end.tv_usec - start.tv_usec)) / ((int)1e6);
    int usecs = (int)((1e6 * end.tv_sec - 1e6 * start.tv_sec) + (end.tv_usec - start.tv_usec)) % ((int)1e6);
    std::cout << "Took " << secs << " seconds and " << usecs << " micro seconds" << endl;

    // quitting and closing all channels in FIFO

    MESSAGE_TYPE q = QUIT_MSG;

    for (size_t i = 0; i < fChans.size(); i++)
    {
        fChans.at(i)->cwrite((char *)&q, sizeof(MESSAGE_TYPE));
        delete fChans.at(i);
    }

    // quit and close control channel
    chan->cwrite((char *)&q, sizeof(MESSAGE_TYPE));
    std::cout << "All Done!" << endl;
    delete chan;

    // wait for server to exit
    wait(nullptr);
}
