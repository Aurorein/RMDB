/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL
v2. You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_scan.h"
#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
  // 初始化file_handle和rid（指向第一个存放了记录的位置）
  rid_.page_no = RM_FIRST_RECORD_PAGE; // 记录所在数据页初始化为1
  rid_.slot_no = -1;
  next();
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
  // 找到文件中下一个存放了记录的非空闲位置，用rid_来指向这个位置
  if (is_end()) {
    return;
  }
  // 遍历所有Page
  for (; rid_.page_no < file_handle_->file_hdr_.num_pages; rid_.page_no++) {
    // 用位图找到下一个为1的位, [rid.slot_no + 1, page.num_records)
    int num_record = file_handle_->file_hdr_.num_records_per_page;
    auto page_hdl = file_handle_->fetch_page_handle(rid_.page_no);
    rid_.slot_no = Bitmap::next_bit(1, page_hdl.bitmap, num_record, rid_.slot_no);
    // 记得unpin
    file_handle_->buffer_pool_manager_->unpin_page(page_hdl.page->get_page_id(), false);
    if (rid_.slot_no < num_record) {
      return;
    }
    rid_.slot_no = -1;
  }
  // 遍历Page都没找到
  rid_.page_no = RM_NO_PAGE;
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
  // 判断数据页id是否没有下一页
  return rid_.page_no == RM_NO_PAGE;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const { return rid_; }