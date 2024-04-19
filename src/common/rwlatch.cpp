// #pragma once
#include "rwlatch.h"

void RWLatch::RLock(){
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return write_count_ == 0; });
    read_count_++;
    
}

void RWLatch::RUnlock(){
    std::unique_lock<std::mutex> lock(mutex_);
    read_count_--;
    if (read_count_ == 0) {
        cv_.notify_one();
    }
}

void RWLatch::WLock(){
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return read_count_ == 0 && write_count_ == 0; });
    write_count_++;
}

void RWLatch::WUnlock(){
    // if(write_count_ > 0) {
    //     std::unique_lock<std::mutex> lock(mutex_);
    //     write_count_--;
    //     cv_.notify_all();
    // }

    std::unique_lock<std::mutex> lock(mutex_);
    write_count_--;
    cv_.notify_all();
}