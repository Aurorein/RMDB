set(SOURCES 
        disk_manager.cpp 
        buffer_pool_manager.cpp 
        ../replacer/replacer.h 
        ../replacer/lru_replacer.cpp 
        # ../recovery/log_manager.h
)
add_library(storage STATIC ${SOURCES})
target_link_libraries(storage recovery)