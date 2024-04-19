#pragma once

#include <memory>

#include "execution/executor_abstract.h"
#include "record/rm_defs.h"

class JoinBuffer {
public:
    const static int JOIN_BUFFER_SIZE = 1000;
    std::unique_ptr<RmRecord> buffer_[JOIN_BUFFER_SIZE];
    size_t size_; // 记录大小
    size_t cur_pos_;

    JoinBuffer() : size_(0), cur_pos_(0) {}

    void push_back(std::unique_ptr<RmRecord> record) {
        assert(!is_full());    
        buffer_[size_] = std::move(record);
        size_++;
    }

    void beginTuple() {
        cur_pos_ = 0;
    }

    void nextTuple() {
        cur_pos_++;
    }

    std::unique_ptr<RmRecord>* get_record() {
        assert(!is_end());
        return &buffer_[cur_pos_];
    }

    void reset() {
        memset(buffer_, 0, sizeof(buffer_));
        cur_pos_ = 0;
        size_ = 0;
    }
 
    bool is_full() {
        return size_ >= JOIN_BUFFER_SIZE;
    }

    bool is_end() {
        return cur_pos_ >= size_;
    }

};


class ExecutorWithJoinBuffer {
public:
    std::unique_ptr<AbstractExecutor>* executor_;
    std::unique_ptr<JoinBuffer> join_buffer_;
    ExecutorWithJoinBuffer(std::unique_ptr<AbstractExecutor>* exe) : executor_(exe) {
        join_buffer_ = std::make_unique<JoinBuffer>();
    }

    void beginBuffer() {
        (*executor_)->beginTuple(); 
        join_buffer_->reset();
        while(!(*executor_)->is_end() && !join_buffer_->is_full()) {
            join_buffer_->push_back(std::move((*executor_)->Next()));
            (*executor_)->nextTuple();
        }
    }

    void nextBuffer() {
        assert(!is_end());
        join_buffer_->reset();
        while(!(*executor_)->is_end() && !join_buffer_->is_full()) {
            join_buffer_->push_back(std::move((*executor_)->Next()));
            (*executor_)->nextTuple();
        }
    }

    std::unique_ptr<JoinBuffer>* Next() {
        // assert(!is_end());
        return &join_buffer_;
    }

    bool is_end() {
        return join_buffer_->size_ == 0;
    }
};