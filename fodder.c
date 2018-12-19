/*
* Copyright (C) 2018 Intel Corporation
* Authors: Qin, Zhang
*
* This software may be redistributed and/or modified under the terms of
* the GNU General Public License ("GPL") version 2 only as published by the
* Free Software Foundation.
*/
/*
* Set up to get zapped by a machine check (injected elsewhere)
* recovery function reports physical address of new page - so
* we can inject to that and repeat over and over.
* With "-t" flag report physical address of a ".text" (code) page
* so we will test the instruction fault path - otherwise report
* an allocated data page.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
static int pagesize;
/*
* get information about address from /proc/{pid}/pagemap
*/
unsigned long long vtop(unsigned long long addr) {
    unsigned long long pinfo;
    long offset = addr / pagesize * (sizeof pinfo);
    int fd;
    char pagemapname[64];
    sprintf(pagemapname, "/proc/%d/pagemap", getpid());
    fd = open(pagemapname, O_RDONLY);
    if (fd == -1) {
        perror(pagemapname);
        exit(1);
    }
    if (pread(fd, &pinfo, sizeof pinfo, offset) != sizeof pinfo) {
        perror(pagemapname);
        exit(1);
    }
    close(fd);
    if ((pinfo & (1ull << 63)) == 0) {
        printf("page not present\n");
        exit(1);
    }
    return ((pinfo & 0x007fffffffffffffull) << 12) + (addr & (pagesize - 1));
}
/*
* Read /proc/kpageflags to find out status of a page
*/
struct kpageflagsbits {
    unsigned long locked : 1;
    unsigned long error : 1;
    unsigned long referenced : 1;
    unsigned long uptodate : 1;
    unsigned long dirty : 1;
    unsigned long lru : 1;
    unsigned long active : 1;
    unsigned long slab : 1;
    unsigned long writeback : 1;
    unsigned long reclaim : 1;
    unsigned long buddy : 1;
    unsigned long mmap : 1;
    unsigned long anon : 1;
    unsigned long swapcache : 1;
    unsigned long swapbacked : 1;
    unsigned long compound_head : 1;
    unsigned long compound_tail : 1;
    unsigned long huge : 1;
    unsigned long unevictable : 1;
    unsigned long poison : 1;
    unsigned long nopage : 1;
    unsigned long ksm : 1;
    unsigned long thp : 1;
    unsigned long reserved : 41;
};
static void kpageflags(unsigned long long pfn) {
    struct kpageflagsbits bits;
    int fd = open("/proc/kpageflags", O_RDONLY);
    if (fd == -1) {
        perror("/proc/kpageflags");
        exit(1);
    }
    if (pread(fd, &bits, sizeof bits, pfn*sizeof(bits)) != sizeof bits) {
        perror("pread(/proc/kpageflags)");
        exit(1);
    }
    printf("flags for page %llx:", pfn);
    if (bits.locked) printf(" locked");
    if (bits.error) printf(" error");
    if (bits.referenced) printf(" referenced");
    if (bits.uptodate) printf(" uptodate");
    if (bits.dirty) printf(" dirty");
    if (bits.lru) printf(" lru");
    if (bits.active) printf(" active");
    if (bits.slab) printf(" slab");
    if (bits.writeback) printf(" writeback");
    if (bits.reclaim) printf(" reclaim");
    if (bits.buddy) printf(" buddy");
    if (bits.mmap) printf(" mmap");
    if (bits.anon) printf(" anon");
    if (bits.swapcache) printf(" swapcache");
    if (bits.swapbacked) printf(" swapbacked");
    if (bits.compound_head) printf(" compound_head");
    if (bits.compound_tail) printf(" compound_tail");
    if (bits.huge) printf(" huge");
    if (bits.unevictable) printf(" unevictable");
    if (bits.poison) printf(" poison");
    if (bits.nopage) printf(" nopage");
    if (bits.ksm) printf(" ksm");
    if (bits.thp) printf(" thp");
    printf("\n");
}
volatile long total;
int peek(void *addr) {
    return *(int *)addr;
}
/*
* Older glibc headers don't have the si_addr_lsb field in the siginfo_t
* structure ... ugly hack to get it
*/
struct morebits {
    void *addr;
    short lsb;
};
char *buf;
unsigned long long phys;
/*
* "Recover" from the error by allocating a new page and mapping
* it at the same virtual address as the page we lost. Fill with
* the same (trivial) contents.
*/
void recover(int sig, siginfo_t *si, void *v) {
    struct morebits *m = (struct morebits *)&si->si_addr;
    char *newbuf;
    printf("recover: sig=%d si=%p v=%p\n", sig, si, v);
    printf("Platform memory error at 0x%p\n", si->si_addr);
    printf("addr = %p lsb=%d\n", m->addr, m->lsb);
    newbuf = mmap(buf, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC,
                    MAP_FIXED|MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    if (newbuf == MAP_FAILED) {
        fprintf(stderr, "Can't get a single page of memory!\n");
        exit(1);
    }
    if (newbuf != buf) {
        fprintf(stderr, "Could not allocate at original virtual address\n");
            exit(1);
    }
    buf = newbuf;
    memset(buf, '*', pagesize);
    phys = vtop((unsigned long long)buf);
    printf("Recovery allocated new page at physical %llx\n", phys);
}
struct sigaction recover_act = {
.sa_sigaction = recover,
.sa_flags = SA_SIGINFO,
};
/* Don't let compiler optimize away access to this */
volatile int ifunc_ret;
#define PLUS10 (ifunc_ret++,ifunc_ret++,ifunc_ret++,ifunc_ret++,ifunc_ret++,ifunc_ret++,ifunc_ret++,ifunc_ret++,ifunc_ret++,ifunc_ret++)
#define PLUS100 (PLUS10,PLUS10,PLUS10,PLUS10,PLUS10,PLUS10,PLUS10,PLUS10,PLUS10,PLUS10)
#define PLUS1000 (PLUS100,PLUS100,PLUS100,PLUS100,PLUS100,PLUS100,PLUS100,PLUS100,PLUS100,PLUS100)
/*
* Use above macros to make this a non-trivial sized function.
*/
int ifunc(void) {
    ifunc_ret = 0;
    PLUS1000;
    return ifunc_ret;
}
int main(int argc, char **argv) {
    char answer[16];
    int i, n;
    unsigned long long phys;
    int tflag = 0;
    time_t now;
    pagesize = getpagesize();
    if (argc > 1 && strcmp(argv[1], "-t") == 0)
        tflag = 1;
    if (tflag) {
        buf = (char *)(((unsigned long)ifunc + (pagesize-1)) & ~(pagesize-1));
        printf("ifunc() = %d\n", ifunc());
    } else {
        buf = mmap(NULL, pagesize, PROT_READ|PROT_WRITE|PROT_EXEC,
                        MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
    if (buf == MAP_FAILED) {
        fprintf(stderr, "Can't get a single page of memory!\n");
        return 1;
    }
    memset(buf, '*', pagesize);
    }
    phys = vtop((unsigned long long)buf);
    sigaction(SIGBUS, &recover_act, NULL);
    kpageflags(phys / pagesize);
    printf("vtop(%llx) = %llx\nHit any key to access: ", (unsigned long long)buf, phys);
    fflush(stdout);
    n = read(0, answer, 16);
    now = time(NULL);
    printf("Access at %s\n", ctime(&now));
    kpageflags(phys / pagesize);
    if (tflag) {
        while (ifunc() == 1000) {
            if (vtop((unsigned long long)buf) != phys) {
                phys = vtop((unsigned long long)buf);
                kpageflags(phys / pagesize);
                printf("NEW vtop(%llx) = %llx\nHit any key to access: ", (unsigned long long)buf, phys);
                fflush(stdout);
                n = read(0, answer, 16);
            }
        }
    } else {
        printf("Got %x\n", peek(buf));
        while (1) {
            for (i = 0; i < 0x1000; i+=4)
                total += peek(buf + i);
        }
    }
    return 0;
}
