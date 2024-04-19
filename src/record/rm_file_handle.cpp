/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 初始化一个指向RmRecord的指针（赋值其内部的data和size）

    // std::scoped_lock lock{latch_};

    // get record加S锁
    if(context != nullptr) {
        context->lock_mgr_->lock_shared_on_record_wait_time(context->txn_, rid, fd_);
    }

    // 1.获取记录所在的page handle
    RmPageHandle page_hdl = fetch_page_handle(rid.page_no);

    // 2.1 初始化一个指向RmRecord的指针
    int record_size = file_hdr_.record_size;
    auto rm_rcd = std::make_unique<RmRecord>(record_size);
    
    // 2.2 赋值RmRecord指针内部的data和size
    char* data_ptr = page_hdl.get_slot(rid.slot_no);
    // rm_rcd->data = data_ptr;
    std::memcpy(rm_rcd->data,data_ptr,record_size);
    rm_rcd->size = record_size;

    buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(), false);
    // 3. 将RmRecord封装为unique_ptr并返回 
    return rm_rcd;
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char* buf, Context* context, const std::string tab_name) {
    // Todo:
    // 1. 获取当前未满的page handle
    // 2. 在page handle中找到空闲slot位置
    // 3. 将buf复制到空闲slot位置
    // 4. 更新page_handle.page_hdr中的数据结构
    // 注意考虑插入一条记录后页面已满的情况，需要更新file_hdr_.first_free_page_no

    // 1. 获取当前未满的page Handle
    // int page_no = file_hdr_.first_free_page_no;
    // RmPageHandle *page_hdl_ptr;
    // if(page_no == -1){
    //     // 1.1 如果不存在有未满的page handle，则创建一个新的page handle
    //     page_hdl_ptr = &create_new_page_handle();
    // }else{
    //     // 1.2 如果有空闲的page handle，得到该page handle
    //     page_hdl_ptr = &fetch_page_handle(page_no);
    // }

    // std::scoped_lock lock{latch_};

    RmPageHandle page_hdl = create_page_handle();

    // 2. 在page_hdl中找到空闲slot位置
    int record_size = file_hdr_.record_size;
    int record_nums = file_hdr_.num_records_per_page;
    int slot_no = Bitmap::first_bit(false, page_hdl.bitmap, record_nums);
    if(slot_no == record_nums){ 
        throw InvalidSlotNoError(slot_no, record_nums);
    }

    // insert_entry对record加X锁
    if(context != nullptr) {
        auto rid = Rid{.page_no = page_hdl.page->get_page_id().page_no, .slot_no = slot_no};
        context->lock_mgr_->lock_exclusive_on_record_wait_time(context->txn_, rid, fd_);
    }

    // 3. 将buf复制到空闲slot位置
    char* slot_ptr = page_hdl.get_slot(slot_no);
    std::memcpy(slot_ptr, buf, record_size);
    Bitmap::set(page_hdl.bitmap, slot_no);

    // 4. 更新page_handle.page_hdr的数据结构
    // 4.1 当插入该record以后，该页面满了，更新RmFileHdr的first_page_no
    if(++page_hdl.page_hdr->num_records == record_nums){
        file_hdr_.first_free_page_no = page_hdl.page_hdr->next_free_page_no;
    }

    buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(), true);
    // 5. 返回新插入的record的rid
    return Rid{page_hdl.page->get_page_id().page_no,slot_no};
}

void RmFileHandle::massive_insert(const std::vector<std::unique_ptr<RmRecord>> *records, std::vector<Rid> *rids, Context *context) {
    if(rids != nullptr) {
        assert(rids->size() == 0);
    }

    int record_size = file_hdr_.record_size;
    int record_nums = file_hdr_.num_records_per_page;

    RmPageHandle page_hdl = create_page_handle();
    for(size_t i = 0; i < records->size(); i++) {
        int slot_no = Bitmap::first_bit(false, page_hdl.bitmap, record_nums);
        if(slot_no == record_nums){ 
            throw InvalidSlotNoError(slot_no, record_nums);
        }
        // insert_entry对record加X锁
        auto rid = Rid{.page_no = page_hdl.page->get_page_id().page_no, .slot_no = slot_no};
        if(context != nullptr) {
            context->lock_mgr_->lock_exclusive_on_record_wait_time(context->txn_, rid, fd_);
        }
        //
        if(rids != nullptr) {
            rids->push_back(rid);
        }
        // 3. 将buf复制到空闲slot位置
        char* slot_ptr = page_hdl.get_slot(slot_no);
        std::memcpy(slot_ptr, (*records)[i]->data, record_size);
        Bitmap::set(page_hdl.bitmap, slot_no);

        // 当前page满了，取下一个page
        if(++page_hdl.page_hdr->num_records == record_nums){
            file_hdr_.first_free_page_no = page_hdl.page_hdr->next_free_page_no;
            buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(), true);
            // buffer_pool_manager_->flush_page(page_hdl.page->get_page_id());
            page_hdl = create_page_handle();
        }
    }
    buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(), true);
    // flush all pages
    // buffer_pool_manager_->flush_page(page_hdl.page->get_page_id());
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
    // Todo:
    // 1. 判断指定位置是否空闲，若不空闲则返回异常
    // 2. 在指定位置插入记录
    
    // 1. 判断指定位置是否空闲
    // bool is_rcd = is_record(rid);
    // if(is_rcd){
    //     throw RecordNotFoundError(rid.page_no,rid.slot_no);
    // }

    // std::scoped_lock lock{latch_};

    // 2. 获取rid对应的page handle对象
    RmPageHandle page_hdl = fetch_page_handle(rid.page_no);
    assert(!Bitmap::is_set(page_hdl.bitmap, rid.slot_no));   

    // 3. 在指定位置插入记录
    int record_size = file_hdr_.record_size;
    char* slot_ptr = page_hdl.get_slot(rid.slot_no);
    std::memcpy(slot_ptr, buf, record_size);

    // 4. 更新page_handle中的数据结构
    Bitmap::set(page_hdl.bitmap, rid.slot_no);
    int record_nums = file_hdr_.num_records_per_page;
    if(++page_hdl.page_hdr->num_records == record_nums){
        // 感觉不太对劲？为什么它作为第一个空闲的？
        // TODO 修改
        file_hdr_.first_free_page_no = page_hdl.page_hdr->next_free_page_no;
    }

    buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(), true);
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新page_handle.page_hdr中的数据结构
    // 注意考虑删除一条记录后页面未满的情况，需要调用release_page_handle()


    // std::scoped_lock lock{latch_};

    // delete entry对record加X锁
    if(context != nullptr) {
        context->lock_mgr_->lock_exclusive_on_record_wait_time(context->txn_, rid, fd_);
    }
    // 1. 获取指定记录所在的page handle
    RmPageHandle page_hdl = fetch_page_handle(rid.page_no);


    // 2. 更新page_handle.page_hdr的数据结构
    // 2.1 测试bitmap对应位,测试成功则重置
    if(!Bitmap::is_set(page_hdl.bitmap,rid.slot_no)){
        throw RecordNotFoundError(rid.page_no,rid.slot_no);
    }
    Bitmap::reset(page_hdl.bitmap,rid.slot_no);
    // 2.2 如果page从满变为未满状态,调用release_page_handle()

    // int record_size = file_hdr_.record_size;
    int record_nums = file_hdr_.num_records_per_page;
    if(page_hdl.page_hdr->num_records-- == record_nums){
        release_page_handle(page_hdl);
    }   

    // 更新lsn
    // page_hdl.page->set_page_lsn(context->txn_->get_prev_lsn());

    buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(), true); 

}


/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新记录

    // std::scoped_lock lock{latch_};

    // update_entry对record加X锁
    if(context != nullptr) {
        context->lock_mgr_->lock_exclusive_on_record_wait_time(context->txn_, rid, fd_);
    }

    // 1. 获取指定记录所在的page handle
    RmPageHandle page_hdl = fetch_page_handle(rid.page_no);

    if(!Bitmap::is_set(page_hdl.bitmap, rid.slot_no)){
        throw RecordNotFoundError(rid.page_no,rid.slot_no);
    }
    // 2. 更新记录
    int record_size = file_hdr_.record_size;
    char* slot_ptr = page_hdl.get_slot(rid.slot_no);
    std::memcpy(slot_ptr, buf, record_size);

    // 3. 确保bitmap标识为存有记录
    // Bitmap::set(page_hdl.bitmap,rid.slot_no);

    // 更新lsn
    // page_hdl.page->set_page_lsn(context->txn_->get_prev_lsn());

    buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(), true);
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    // Todo:
    // 使用缓冲池获取指定页面，并生成page_handle返回给上层
    // if page_no is invalid, throw PageNotExistError exception

    // 1.如果page_no invalid，抛出PageNotExistError异常
    if(page_no >= RmFileHandle::file_hdr_.num_pages){
        throw PageNotExistError("",page_no);
    }
    // 2.通过buffer pool manager获取指定pageId的page
    PageId page_id;
    page_id.page_no = page_no;
    page_id.fd = fd_;
    Page *page = buffer_pool_manager_->fetch_page(page_id);

    if(page == nullptr){
        throw PageNotExistError("",page_no);
    }
    // 3.将获取到的page对象封装为RmPageHandle返回
    RmPageHandle page_hdr(&file_hdr_,page);
    return page_hdr;
}

void RmFileHandle::update_page_lsn(int page_no, lsn_t lsn) const{
    // 1.如果page_no invalid，抛出PageNotExistError异常
    if(page_no >= RmFileHandle::file_hdr_.num_pages){
        throw PageNotExistError("",page_no);
    }
    // 2.通过buffer pool manager获取指定pageId的page
    PageId page_id;
    page_id.page_no = page_no;
    page_id.fd = fd_;
    Page *page = buffer_pool_manager_->fetch_page(page_id);

    if(page == nullptr){
        throw PageNotExistError("",page_no);
    }

    // 3. 更新lsn
    page->set_page_lsn(lsn);

    // 4. unpin该page
    buffer_pool_manager_->unpin_page(page_id,true);
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    // Todo:
    // 1.使用缓冲池来创建一个新page
    // 2.更新page handle中的相关信息
    // 3.更新file_hdr_

    // 1. 使用缓冲池创建一个新的page
    PageId page_id;
    page_id.fd = fd_;
    Page *page = buffer_pool_manager_->new_page(&page_id);

    // 2. 更新page handle的相关信息
    RmPageHandle page_hdl = RmPageHandle(&file_hdr_, page);
    Bitmap::init(page_hdl.bitmap,file_hdr_.bitmap_size);
    page_hdl.page_hdr->num_records = 0;
    page_hdl.page_hdr->next_free_page_no = RM_NO_PAGE;
    

    //3. 更新file_hdr_
    file_hdr_.num_pages++;
    file_hdr_.first_free_page_no = page_hdl.page->get_page_id().page_no;

    // 4. 返回page_hdl
    return page_hdl;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    // Todo:
    // 1. 判断file_hdr_中是否还有空闲页
    //     1.1 没有空闲页：使用缓冲池来创建一个新page；可直接调用create_new_page_handle()
    //     1.2 有空闲页：直接获取第一个空闲页
    // 2. 生成page handle并返回给上层

    // 1. 判断file_hdr_中是否还有空闲页
    int page_no = file_hdr_.first_free_page_no;
    if(page_no != RM_NO_PAGE){
        return fetch_page_handle(page_no);
    }else{
        // 2. 生成page handle并返回给上层
        return create_new_page_handle();
    }
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle&page_handle) {
    // Todo:
    // 当page从已满变成未满，考虑如何更新：
    // 1. page_handle.page_hdr->next_free_page_no
    // 2. file_hdr_.first_free_page_no
    
    // 1. page_handle.page_hdr->next_free_page_no
    // 注意：RmFileHandle的first_free_page_no不一定非要是page_no最小的，因为是链表结构
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;

    // 2. file_hdr_.first_free_page_no
    file_hdr_.first_free_page_no = page_handle.page->get_page_id().page_no;
}