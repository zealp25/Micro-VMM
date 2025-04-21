/* Sample code for /dev/kvm API
 *
 * Copyright (c) 2015 Intel Corporation
 * Author: Josh Triplett <josh@joshtriplett.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <err.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <ncurses.h>
#include <time.h>

#define IO_PORT_CONSOLE 0x42
#define IO_PORT_KEYBOARD 0x44
#define IO_PORT_KEYBOARD_STATUS 0x45
#define IO_PORT_TIMER_VALUE 0x46
#define IO_PORT_TIMER_CONTROL 0x47
#define CONSOLE_BUFFER_SIZE 500
#define KEYBOARD_BUFFER_SIZE 32

struct {
    uint8_t buffer[KEYBOARD_BUFFER_SIZE];
    int head;
    int tail;
} keyboard = {{0}, 0, 0};

void push_key(uint8_t key) {
    int next_tail = (keyboard.tail + 1) % KEYBOARD_BUFFER_SIZE;
    if (next_tail != keyboard.head) {
        keyboard.buffer[keyboard.tail] = key;
        keyboard.tail = next_tail;
    }
}

uint8_t pop_key() {
    if (keyboard.head == keyboard.tail) return 0; // No key available
    uint8_t key = keyboard.buffer[keyboard.head];
    keyboard.head = (keyboard.head + 1) % KEYBOARD_BUFFER_SIZE;
    return key;
}
struct timer_state {
    bool enabled;
    int interval;
    time_t last_fired;
};

void handle_console_io(struct kvm_run *run, char *buffer, size_t *current_size) {
    if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == IO_PORT_CONSOLE && run->io.count == 1) {
        if (*current_size < CONSOLE_BUFFER_SIZE - 1) {
            buffer[*current_size] = *(((char *)run) + run->io.data_offset);
            if (buffer[*current_size] == '\n') {
                buffer[*current_size + 1] = '\0';
                printf("%s", buffer);
                memset(buffer, 0, CONSOLE_BUFFER_SIZE);
                *current_size = 0;
            } else {
                (*current_size)++;
            }
        }
    }
}
void handle_keyboard_io(struct kvm_run *run, bool *keyboard_status, char *keyboard_input) {
    if (run->io.direction == KVM_EXIT_IO_IN && run->io.size == 1 && run->io.port == IO_PORT_KEYBOARD_STATUS) {
        *(((uint8_t *)run) + run->io.data_offset) = *keyboard_status ? 1 : 0;
    } else if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == IO_PORT_KEYBOARD_STATUS) {
        *keyboard_status = false; // Acknowledgement from the guest
    } else if (run->io.direction == KVM_EXIT_IO_IN && run->io.size == 1 && run->io.port == IO_PORT_KEYBOARD) {
        *(((uint8_t *)run) + run->io.data_offset) = *keyboard_input;
    }
}
void handle_timer_io(struct kvm_run *run, struct timer_state *timer) {
    if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == IO_PORT_TIMER_VALUE) {
        timer->interval = *(((uint8_t *)run) + run->io.data_offset);
    } else if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == IO_PORT_TIMER_CONTROL) {
        timer->enabled = *(((uint8_t *)run) + run->io.data_offset) & 1;
        if (timer->enabled) {
            timer->last_fired = time(NULL);
        }
    } else if (run->io.direction == KVM_EXIT_IO_IN && run->io.size == 1 && run->io.port == IO_PORT_TIMER_CONTROL) {
        time_t current_time = time(NULL);
        if (timer->enabled && difftime(current_time, timer->last_fired) * 1000 >= timer->interval) {
            *(((uint8_t *)run) + run->io.data_offset) = 1 << 1; // Timer event flag
            timer->last_fired = current_time;
        } else {
            *(((uint8_t *)run) + run->io.data_offset) = 0;
        }
    }
}
void check_error(int result, const char* message) {
    if (result == -1) {
        perror(message);
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{   
    if(argc!=2){
        printf("Usage: sudo ./basic-vmm smallkern");
        return EXIT_FAILURE;
    }
    int current_line = 0;
    int kvm, vmfd, vcpufd, ret;
    // const uint8_t code[] = {
    //     0xba, 0xf8, 0x03, /* mov $0x3f8, %dx */
    //     0x00, 0xd8,       /* add %bl, %al */
    //     0x04, '0',        /* add $'0', %al */
    //     0xee,             /* out %al, (%dx) */
    //     0xb0, '\n',       /* mov $'\n', %al */
    //     0xee,             /* out %al, (%dx) */
    //     0xf4,             /* hlt */
    // };

    // const uint8_t code[] = {
    //     0xba, 0x42, 0x00, /* mov $0x3f8, %dx */
    //     0x00, 0xd8,       /* add %bl, %al */
    //     0x04, '0',        /* add $'0', %al */
    //     0xee,             /* out %al, (%dx) */
    //     0xe4, 0x44,  /* + in $0x44, %al */
    //     // 0x04, '0',        /* + add $'0', %al */
    //     0xee,             /* + out %al, (%dx) */
    //     0xb0, '\n',       /* mov $'\n', %al */
    //     0xee,             /* out %al, (%dx) */
    //     0xf4,             /* hlt */
    // };

    // const uint8_t code[] = {
    //     0xe4, 0x45,                   //in     al,0x45
    //     0x3c, 0x00,                  //cmp    al,0x0
    //     0x74, 0xfa,                // je     0 <_start>
    //     0xe4, 0x44,                  // in     al,0x44
    //     0xba, 0x42, 0x00,             // mov    dx,0x42
    //     0xee,                     // out    dx,al
    //     0xb0, 0x0a,                  // mov    al,0xa
    //     0xee,                     //out    dx,al
    //     0xf4,                      //hlt  
    // };

    uint8_t *mem;
    struct kvm_sregs sregs;
    size_t mmap_size;
    struct kvm_run *run;

    kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm == -1)
        err(1, "/dev/kvm");

    /* Make sure we have the stable version of the API */
    ret = ioctl(kvm, KVM_GET_API_VERSION, NULL);
    if (ret == -1)
        err(1, "KVM_GET_API_VERSION");
    if (ret != 12)
        errx(1, "KVM_GET_API_VERSION %d, expected 12", ret);

    vmfd = ioctl(kvm, KVM_CREATE_VM, (unsigned long)0);
    if (vmfd == -1)
        err(1, "KVM_CREATE_VM");

    /* Allocate one aligned page of guest memory to hold the code. */
    mem = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (!mem)
        err(1, "allocating guest memory");

	FILE *file;
	char *code_buffer;
	unsigned long fileLen;

	//Open file
	file = fopen(argv[1], "rb");
	if (!file)
	{
		fprintf(stderr, "Unable to open file %s", argv[1]);
        return EXIT_FAILURE;
	}
	
	//Get file length
	fseek(file, 0, SEEK_END);
	fileLen = ftell(file);
	fseek(file, 0, SEEK_SET);

	//Allocate memory
	code_buffer = (char *)malloc(fileLen+1);
	if (!code_buffer)
	{
		fprintf(stderr, "Memory error!");
        fclose(file);
        return EXIT_FAILURE;
	}

	//Read file contents into buffer
	fread(code_buffer, fileLen, 1, file);
	fclose(file);

    memcpy(mem, code_buffer, fileLen);

    /* Map it to the second page frame (to avoid the real-mode IDT at 0). */
    struct kvm_userspace_memory_region region = {
        .slot = 0,
        .flags = KVM_MEM_LOG_DIRTY_PAGES,
        .guest_phys_addr = 0x1000,
        .memory_size = 0x10000,
        .userspace_addr = (uint64_t)mem,
    };
    ret = ioctl(vmfd, KVM_SET_USER_MEMORY_REGION, &region);
    if (ret == -1)
        err(1, "KVM_SET_USER_MEMORY_REGION");

    vcpufd = ioctl(vmfd, KVM_CREATE_VCPU, (unsigned long)0);
    if (vcpufd == -1)
        err(1, "KVM_CREATE_VCPU");

    /* Map the shared kvm_run structure and following data. */
    ret = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, NULL);
    if (ret == -1)
        err(1, "KVM_GET_VCPU_MMAP_SIZE");
    mmap_size = ret;
    if (mmap_size < sizeof(*run))
        errx(1, "KVM_GET_VCPU_MMAP_SIZE unexpectedly small");
    run = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpufd, 0);
    if (!run)
        err(1, "mmap vcpu");

    /* Initialize CS to point at 0, via a read-modify-write of sregs. */
    ret = ioctl(vcpufd, KVM_GET_SREGS, &sregs);
    if (ret == -1)
        err(1, "KVM_GET_SREGS");
    sregs.cs.base = 0;
    sregs.cs.selector = 0;
    ret = ioctl(vcpufd, KVM_SET_SREGS, &sregs);
    if (ret == -1)
        err(1, "KVM_SET_SREGS");

    /* Initialize registers: instruction pointer for our code, addends, and
     * initial flags required by x86 architecture. */
    struct kvm_regs regs = {
        .rip = 0x1000,
        .rax = 2,
        .rbx = 2,
        .rflags = 0x2,
    };
    ret = ioctl(vcpufd, KVM_SET_REGS, &regs);
    if (ret == -1)
        err(1, "KVM_SET_REGS");

    char buffer[500];
    size_t current_size = 0;

    struct{
        bool status;
        char key;
    } keyboard = { .status = false };
    char c;

    struct{
        bool fire;
        bool enable;
        clock_t start_time;
        uint8_t interval;
        uint8_t port47;
    } timer = { .fire = false, .enable = false, .port47 = 0 };
    int time_diff;


    /* Initialize ncurses to immediately make the charater available without waiting for user input */
    initscr();
    cbreak();
    nodelay(stdscr,TRUE);
    noecho();
char input_buffer[CONSOLE_BUFFER_SIZE] = {0}; // A buffer for storing line input
int input_index = 0; 
    /* Repeatedly run code and handle VM exits. */
    while (1) {
        if(timer.enable){
            time_diff = (clock() - timer.start_time)*1000/CLOCKS_PER_SEC;
            // printf("%d", time_diff);
            if(time_diff >= timer.interval){
                timer.port47 |= 1UL << 1;
                timer.start_time = clock();
            }
        }

	       c = getch();
    if (c != ERR) {
        push_key(c);
        printf("Key pressed: %c\n", c);
    }



        ret = ioctl(vcpufd, KVM_RUN, NULL);
        if (ret == -1)
            err(1, "KVM_RUN");
        switch (run->exit_reason) {
        case KVM_EXIT_HLT:
            puts("KVM_EXIT_HLT");
            return 0;
        case KVM_EXIT_IO:
            if (run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == 0x42 && run->io.count == 1){
                // putchar(*(((char *)run) + run->io.data_offset));
                // printf("%c",*(((char *)run) + run->io.data_offset));
                buffer[current_size++] = *(((char *)run) + run->io.data_offset);
                // printf("%c", buffer[current_size-1]);
                if (buffer[current_size-1] == '\n'){
                    buffer[current_size++] = '\0';
                    printf("%s",buffer);
                    memset(buffer, 0, 500);
                    current_size = 0;
                }
            }

            else if(run->io.direction == KVM_EXIT_IO_IN && run->io.size == 1 && run->io.port == 0x45 && run->io.count == 1){
                // printf("port %x", run->io.port);
                // printf("count %x", run->io.count);
                // printf("size %x", run->io.size);
                *(((uint8_t *)run) + run->io.data_offset) = keyboard.status;
                // printf("%c", *(((char *)run) + run->io.data_offset));
            }

            else if(run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == 0x45 && run->io.count == 1){
                keyboard.status = *(((uint8_t *)run) + run->io.data_offset);
            }

            else if(run->io.direction == KVM_EXIT_IO_IN && run->io.size == 1 && run->io.port == 0x44 && run->io.count == 1){
                *(((uint8_t *)run) + run->io.data_offset) = keyboard.key;
            }

            else if(run->io.direction == KVM_EXIT_IO_IN && run->io.size == 1 && run->io.port == 0x47 && run->io.count == 1){
                *(((uint8_t *)run) + run->io.data_offset) = timer.port47;
            }

            else if(run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == 0x47 && run->io.count == 1){
                timer.port47 = *(((uint8_t *)run) + run->io.data_offset);
                if(!timer.enable){
                    timer.enable = (timer.port47 & 1U);
                    if(timer.enable) timer.start_time = clock();
                    // printf("timer enalbed:%u  ", timer.enable);
                } else{
                    timer.enable = (timer.port47 & 1U);
                }
                
            }

            else if(run->io.direction == KVM_EXIT_IO_OUT && run->io.size == 1 && run->io.port == 0x46 && run->io.count == 1){
                timer.interval = *(((uint8_t *)run) + run->io.data_offset);
                // printf("interval %u", timer.interval);
            }
            else
                errx(1, "unhandled KVM_EXIT_IO");
            break;
        case KVM_EXIT_FAIL_ENTRY:
            errx(1, "KVM_EXIT_FAIL_ENTRY: hardware_entry_failure_reason = 0x%llx",
                 (unsigned long long)run->fail_entry.hardware_entry_failure_reason);
        case KVM_EXIT_INTERNAL_ERROR:
            errx(1, "KVM_EXIT_INTERNAL_ERROR: suberror = 0x%x", run->internal.suberror);
        case KVM_EXIT_MMIO:
			printf("Got an unexpected MMIO exit:"
					   " phys_addr %#llx,"
					   " data %02x %02x %02x %02x"
						" %02x %02x %02x %02x,"
					   " len %u, is_write %hhu",
					   (unsigned long long) run->mmio.phys_addr,
					   run->mmio.data[0], run->mmio.data[1],
					   run->mmio.data[2], run->mmio.data[3],
					   run->mmio.data[4], run->mmio.data[5],
					   run->mmio.data[6], run->mmio.data[7],
					   run->mmio.len, run->mmio.is_write);

        default:
            errx(1, "exit_reason = 0x%x", run->exit_reason);
        }
    }
}
