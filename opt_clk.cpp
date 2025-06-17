#include <csignal>
#include <iostream>

volatile bool keep_running = true;

void handle_sigint(int) {
    keep_running = false;
}

int main() {
    std::signal(SIGINT, handle_sigint);

    uint64_t ticks = 0;

    asm volatile (
        "mov %[ticks], %%rax\n\t"    // Move ticks to RAX
        "1:\n\t"
        "inc %%rax\n\t"              // Increment RAX
        "cmpb $0, %[keep_running]\n\t"
        "jne 1b\n\t"
        "mov %%rax, %[ticks]\n\t"    // Store RAX back to ticks
        : [ticks] "+r" (ticks)
        : [keep_running] "m" (keep_running)
        : "rax"
    );

    std::cout << "Ticks: " << ticks << std::endl;
    return 0;
}
