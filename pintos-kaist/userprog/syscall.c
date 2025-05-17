#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"   // 💡 있으면 좋고
#include "userprog/process.h"

extern struct lock filesys_lock;

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void check_address(const void *addr);
struct file *process_get_file(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}


void
syscall_handler (struct intr_frame *f) {
	uint64_t syscall_num = f->R.rax;

	switch (syscall_num)
	{
	case SYS_HALT:
		power_off();
		break;
	case SYS_EXIT:
		sys_exit((int)f->R.rdi);
		break;
	case SYS_EXEC:
		if (process_exec((void *)f->R.rdi) == -1)
		sys_exit(-1);
		NOT_REACHED();
	break;
	case SYS_WRITE:
		f->R.rax = write((int)f->R.rdi, (const void *)f->R.rsi, (unsigned)f->R.rdx);
		break;
	default:
		printf("Unknown system call: %lld\n", syscall_num);
		thread_exit();
	}
}

int write(int fd, const void *buffer, unsigned size) {
    check_address(buffer);
    struct file *file = process_get_file(fd);
    /* 실행된 후 쓰여진 바이트 수를 저장하는 변수 */
    int bytes_written = 0;

    lock_acquire(&filesys_lock);

    if (fd == STDOUT_FILENO) {
        /* 쓰기가 표준 출력인 경우, 버퍼의 내용을 화면에 출력하고 쓰여진 바이트 수를 저장 */
        putbuf(buffer, size);
        bytes_written = size;
    } else if (fd == STDIN_FILENO) {
        /* 쓰기가 표준 입력인 경우, 파일 시스템 잠금 해제 후 -1 반환 */
        lock_release(&filesys_lock);
        return -1;
    } else if (fd >= 2) {
        if (file == NULL) {
            /* 쓰기가 파일에 대한 것인데 파일이 없는 경우, 파일 시스템 잠금 해제 후 -1 반환 */
            lock_release(&filesys_lock);
            return -1;
        }
        /* 파일에 버퍼의 내용을 쓰고 쓰여진 바이트 수를 저장 */
        bytes_written = file_write(file, buffer, size);
    }

    lock_release(&filesys_lock);

    return bytes_written;
}

void sys_exit(int status) {
	struct thread *cur = thread_current();
	cur->exit_status = status;

	thread_exit();
}

void check_address(const void *addr){
	struct thread *cur = thread_current();

	if(addr == NULL || !is_user_vaddr(addr) || pml4_get_page(cur -> pml4, addr) == NULL){
		sys_exit(-1);
	}
}

struct file *process_get_file(int fd){
	struct thread *cur = thread_current();

	if(fd < 2 || fd >= 128){
		return NULL;
	}
	return cur -> fdt[fd];
}