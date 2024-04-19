/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "execution_defs.h"
#include "common/common.h"
#include "index/ix.h"
#include "system/sm.h"
#include "defs.h"

class AbstractExecutor {
   public:
    Rid _abstract_rid;

    Context *context_;

    virtual ~AbstractExecutor() = default;

    virtual size_t tupleLen() const { return 0; };

    virtual const std::vector<ColMeta> &cols() const {
        std::vector<ColMeta> *_cols = nullptr;
        return *_cols;
    };

    virtual std::string getType() { return "AbstractExecutor"; };

    virtual void beginTuple(){};

    virtual void nextTuple(){};

    virtual bool is_end() const { return true; };

    virtual Rid &rid() = 0;

    virtual std::unique_ptr<RmRecord> Next() = 0;

    virtual ColMeta get_col_offset(const TabCol &target) { return ColMeta();};

    std::vector<ColMeta>::const_iterator get_col(const std::vector<ColMeta> &rec_cols, const TabCol &target) {
        auto pos = std::find_if(rec_cols.begin(), rec_cols.end(), [&](const ColMeta &col) {
            return col.tab_name == target.tab_name && col.name == target.col_name;
        });
        if (pos == rec_cols.end()) {
            throw ColumnNotFoundError(target.tab_name + '.' + target.col_name);
        }
        return pos;
    }

    // 从Record中取出某一列的Value
    Value fetch_value(const std::unique_ptr<RmRecord> &record, const ColMeta& col) const {
        char *data = record->data + col.offset;
        size_t len = col.len;
        Value ret;
        ret.type = col.type;
        // ret.init_raw(len);
        if(col.type == TYPE_INT) {
            int tmp;
            memcpy((char*)&tmp, data, len);
            ret.set_int(tmp);
        }else if(col.type == TYPE_FLOAT) {
            float tmp;
            memcpy((char*)&tmp, data, len);
            ret.set_float(tmp);
        }else if(col.type == TYPE_STRING) {
            std::string tmp(data, len);
            ret.set_str(tmp);
        }else if(col.type == TYPE_BIGINT) {
            int64_t tmp;
            memcpy((char*)&tmp, data, len);
            ret.set_bigint(tmp);
        }else if(col.type == TYPE_DATETIME) {
            uint64_t tmp;
            memcpy((char*)&tmp, data, len);
            ret.set_datetime(tmp);
        }else {
            throw InvalidTypeError();
        }
        ret.init_raw(len);
        return ret;
    }

    // compare_value的功能是比较left_vlaue和right_value的值是否符合CompOp中定义的op
    bool compare_value(const Value& left_value, const Value& right_value, CompOp op) const {
        // 检查type是否一致
        if(!check_type(left_value.type, right_value.type)) {
            throw IncompatibleTypeError(coltype2str(left_value.type), coltype2str(right_value.type));
        }
        switch (op)
        {
            case OP_EQ:
                return left_value == right_value; 
            case OP_NE: 
                return left_value != right_value;
            case OP_LT: 
                return left_value < right_value;
            case OP_GT: 
                return left_value > right_value;
            case OP_LE: 
                return left_value <= right_value;
            case OP_GE:
                return left_value >= right_value;
        }
        throw IncompatibleTypeError(coltype2str(left_value.type), coltype2str(right_value.type));
    }
};