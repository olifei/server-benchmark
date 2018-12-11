#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#define PAGEMAP_ENTRY 8
#define GET_BIT(X,Y) (X & ((uint64_t)1<<Y)) >> Y
#define GET_PFN(X) X & 0x7FFFFFFFFFFFFF

const int __endian_bit = 1;
#define is_bigendian() ( (*(char*)&__endian_bit) == 0 )


uint64_t read_pagemap(char * path_buf, uint64_t virt_addr){
    uint64_t read_val, file_offset, page_size;
    int i, c, status;
    FILE * f;
    f = fopen(path_buf, "rb");
    if(!f){
        printf("Error! Cannot open %s\n", path_buf);
        return -1;
    }

    //Shifting by virt-addr-offset number of bytes
    //and multiplying by the size of an address (the size of an entry in pagemap file)
    page_size = getpagesize();
    file_offset = virt_addr / page_size * PAGEMAP_ENTRY;
    status = fseek(f, file_offset, SEEK_SET);
    if(status){
        perror("Failed to do fseek!");
        return -1;
    }
    errno = 0;
    read_val = 0;
    unsigned char c_buf[PAGEMAP_ENTRY];
    for(i=0; i < PAGEMAP_ENTRY; i++){
        c = getc(f);
        if(c==EOF){
            return 0;
        }
        if(is_bigendian())
            c_buf[i] = c;
        else
            c_buf[PAGEMAP_ENTRY - i - 1] = c;
        //printf("[%d]0x%x ", i, c);
    }
    for(i=0; i < PAGEMAP_ENTRY; i++){
        //printf("%d ",c_buf[i]);
        read_val = (read_val << 8) + c_buf[i];
    }
    //printf("Result: 0x%llx\n", (unsigned long long) read_val);
    if(GET_BIT(read_val, 63)) {
        uint64_t pfn = GET_PFN(read_val);
        // printf("PFN: 0x%lx (0x%lx)\n", pfn, pfn * page_size + virt_addr % page_size);
        return pfn * page_size + virt_addr % page_size;
    } else
        printf("Page not present\n");
    if(GET_BIT(read_val, 62))
        printf("Page swapped\n");
    fclose(f);
    return 0;
}

int main(int argc, char ** argv){
    printf("Big endian? %d\n", is_bigendian());
    pid_t pid = getpid();
    char path_buf [0x100] = {};
    sprintf(path_buf, "/proc/%u/pagemap", pid);
    printf("path_buf is: %s\n", path_buf);
    printf("size of int type is %d\n", sizeof(int));

    size_t count;
    size_t Miga = 1024*1024;
    size_t Giga = Miga*1024;
    size_t nG = 2;
    size_t intnum = Giga*nG;
    int *bigblock = (int*)malloc(intnum*sizeof(int));
    for(count = 0; count < intnum; count++) {
        *(bigblock+count) = 10;
    }
    for(count = 0; count < intnum; count++) {
        if(count % (Miga*100) == 0){
            printf("vir_addr: 0x%lx\tphy_addr: 0x%lx\n", bigblock+count, read_pagemap(path_buf, (uint64_t)(bigblock+count)));
        }
    }
    return 0;
}
