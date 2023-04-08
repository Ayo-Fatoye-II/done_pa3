#include "BoundedBuffer.h"
#include <iostream>
#include <string.h>
#include <assert.h>


using namespace std;


BoundedBuffer::BoundedBuffer (int _cap) : cap(_cap) {
    // modify as needed
}

BoundedBuffer::~BoundedBuffer () {
    // modify as needed
}

void BoundedBuffer::push (char* msg, int size) {
    // 1. Convert the incoming byte sequence given by msg and size into a vector<char>
    vector<char> _curr(msg, msg + size);
    // 2. Wait until there is room in the queue (i.e., queue lengh is less than cap)
    // 3. Then push the vector at the end of the queue
    // 4. Wake up threads that were waiting for push
    unique_lock<mutex> lk(my_mut);
    wait_to_push.wait(lk, [this]{return q.size() < (size_t) cap;});
    q.push(_curr);
    lk.unlock();
    wait_to_pop.notify_one();
}

int BoundedBuffer::pop (char* msg, int size) {
    // 1. Wait until the queue has at least 1 item
    // 2. Pop the front item of the queue. The popped item is a vector<char>
    // 3. Convert the popped vector<char> into a char*, copy that into msg; assert that the vector<char>'s length is <= size
    // 4. Wake up threads that were waiting for pop
    // 5. Return the vector's length to the caller so that they know how many bytes were popped
    unique_lock<mutex> lk(my_mut);
    wait_to_pop.wait(lk, [this]{return q.size() > 0;});
    vector<char> _front = q.front();
    q.pop();

    assert(_front.size() <= (long unsigned int) size);

    if(nullptr == _front.data()){
        return _front.size();
    }

    memcpy(msg, _front.data(), _front.size());

    lk.unlock();

    wait_to_push.notify_one();

    return _front.size();

}

size_t BoundedBuffer::size () {
    return q.size();
}