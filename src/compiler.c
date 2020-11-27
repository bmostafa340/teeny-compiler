#include <stdio.h>
#include <stdlib.h>

#include "compile.h"
#include "parser.h"

void usage(char *program) {
    fprintf(stderr, "USAGE: %s <program file>\n", program);
    exit(1);
}

/**
 * Prints the start of the the x86-64 assembly output.
 * The assembly code implementing the TeenyBASIC statements
 * goes between the header and the footer.
 */
void header(void) {
    printf(
        "# The code section of the assembly file\n"
        ".text\n"
        ".globl basic_main\n"
        "basic_main:\n"
        "    # The main() function\n"
        "    pushq %%rbp\n"
        "    movq %%rsp, %%rbp\n"
        "    subq $208, %%rsp\n"
        "    push %%rbx\n"
        "    push %%r12\n"
        "    push %%r13\n"
        "    push %%r14\n"
        "    push %%r15\n");
}

/**
 * Prints the end of the x86-64 assembly output.
 * The assembly code implementing the TeenyBASIC statements
 * goes between the header and the footer.
 */
void footer(void) {
    printf(
        "    pop %%r15\n"
        "    pop %%r14\n"
        "    pop %%r13\n"
        "    pop %%r12\n"
        "    pop %%rbx\n"
        "    leaveq\n"
        "    retq\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        usage(argv[0]);
    }

    FILE *program = fopen(argv[1], "r");
    if (program == NULL) {
        usage(argv[0]);
    }

    header();

    node_t *ast = parse(program);
    fclose(program);
    if (ast == NULL) {
        fprintf(stderr, "Parse error\n");
        return 2;
    }

    // Display the AST for debugging purposes
    print_ast(ast);

    // Compile the AST into assembly instructions
    if (!compile_ast(ast)) {
        free_ast(ast);
        fprintf(stderr, "Compilation error\n");
        return 3;
    }

    free_ast(ast);

    footer();
}
