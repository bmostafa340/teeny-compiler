#include "compile.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

uint16_t counter = 0;

typedef struct {
    char **virtual_stack;
    uint8_t num_stack_regs;
    uint8_t stack_index;
    char **var_regs;
    char *vars;
    uint8_t num_vars;
} register_data;

void push_var(char name, register_data *data);
void push_val(int64_t val, register_data *data);
void push_reg(char *reg, register_data *data);
void pop(char *reg, register_data *data);
int8_t const_shift(int64_t value);
int64_t const_operate(char operation, int64_t left, int64_t right);
int64_t constant(node_t *node);
bool optimize(node_t *node, register_data *data);
void asm_operate(binary_node_t *bin_node, register_data *data, bool swap);
bool compile_recursive(node_t *node, register_data *data);
void count_vars(node_t *node, uint8_t *counts);
void choose_vars(uint8_t *counts, uint8_t *top_counts, char *top_vars);
bool compile_ast(node_t *node);

/*
 * Helper for push_var used to get info about where a variable is stored
 * (stack or register, and index in both  cases).
 */
int16_t find_var(char name, register_data *data) {
    int16_t i;
    for (i = 0; i < data->num_vars; i++) {
        if (name == data->vars[i]) {
            return i;
        }
    }
    i = -8 * (name - 'A' + 1);
    return i;
}

/*
 * Pushes a variable's value to the virtual stack.
 */
void push_var(char name, register_data *data) {
    int16_t var_index = find_var(name, data);
    if (var_index >= 0) {
        if (data->stack_index >= data->num_stack_regs) {
            printf("    pushq %s\n", data->var_regs[var_index]);
        }
        else {
            printf("    movq %s, %s\n", data->var_regs[var_index],
                   data->virtual_stack[data->stack_index]);
        }
    }
    else {
        if (data->stack_index >= data->num_stack_regs) {
            printf("    pushq %" PRId16 "(%%rbp)\n", var_index);
        }
        else {
            printf("    movq %" PRId16 "(%%rbp), %s\n", var_index,
                   data->virtual_stack[data->stack_index]);
        }
    }
    (data->stack_index)++;
}

/*
 * Pushes a constant to the virtual stack.
 */
void push_val(int64_t val, register_data *data) {
    if (data->stack_index >= data->num_stack_regs) {
        printf("    movq $%" PRId64 ", %%rax\n", val);
        printf("    pushq %%rax\n");
    }
    else {
        printf("    movq $%" PRId64 ", %s\n", val,
               data->virtual_stack[data->stack_index]);
    }
    (data->stack_index)++;
}

/*
 * Pushes the provided register's value to the virtual stack.
 */
void push_reg(char *reg, register_data *data) {
    if (data->stack_index >= data->num_stack_regs) {
        printf("    pushq %s\n", reg);
    }
    else {
        printf("    movq %s, %s\n", reg, data->virtual_stack[data->stack_index]);
    }
    (data->stack_index)++;
}

/* 
 * Pops the virtual stack to the given register.
 */
void pop(char *reg, register_data *data) {
    if (data->stack_index > data->num_stack_regs) {
        printf("    popq %s\n", reg);
    }
    else {
        printf("    movq %s, %s\n", data->virtual_stack[data->stack_index - 1], reg);
    }
    (data->stack_index)--;
}

/*
 * Attempts to compute k such that value = 2^k.
 * Returns k on success, or max char on fail.
 */
int8_t const_shift(int64_t value) {
    int8_t shift = 0;
    while (value % 2 == 0) {
        value /= 2;
        shift++;
    }
    if (value == 1 || value == -1) {
        shift *= value;
        return shift;
    }
    return __SCHAR_MAX__;
}

/*
 * Performs the provided operation on constants
 */
int64_t const_operate(char operation, int64_t left, int64_t right) {
    if (operation == '+') {
        return left + right;
    }
    else if (operation == '-') {
        return left - right;
    }
    else if (operation == '*') {
        return left * right;
    }
    else if (operation == '/') {
        return left / right;
    }
    exit(1);
}

/* 
 * Prints assembly to perform perform a binary_node operation
 * result should be on top of the virtual stack
 * 
 * Preconditions:
 * If the operation is not a shift:
 *     If swap is false:
 *         The result of the right node is on top of the virtual stack
 *         The result of the left node is immediately below
 *     If swap is true:
 *         The result of the left node is on top of the virtual stack
 *         The result of the right node is immediately below
 * If the operation is a shift:
 *     The result of the right node is in %cl
 *     The result of the left node is on top of the virtual stack
 */
void asm_operate(binary_node_t *bin_node, register_data *data, bool swap) {
    if (bin_node->op == 's') {
        if (data->stack_index > data->num_stack_regs) {
            printf("    salq %%cl, (%%rsp)\n");
        }
        else {
            printf("    sal %%cl, %s\n", data->virtual_stack[data->stack_index - 1]);
        }
    }
    else {
        if (swap) {
            pop("%rax", data);
            pop("%rcx", data);
        }
        else {
            pop("%rcx", data);
            pop("%rax", data);
        }
        bool cmp = false;
        if (bin_node->op == '+') {
            printf("    addq %%rcx, %%rax\n");
        }
        else if (bin_node->op == '*') {
            printf("    imulq %%rcx, %%rax\n");
        }
        else if (bin_node->op == '-') {
            printf("    subq %%rcx, %%rax\n");
        }
        else if (bin_node->op == '/') {
            printf("    cqto\n");
            printf("    idivq %%rcx\n");
        }
        else {
            printf("    cmp %%rcx, %%rax\n");
            cmp = true;
        }
        if (!cmp) {
            push_reg("%rax", data);
        }
    }
}

/* Computes the constant represented by node
 * Precondition: node is an expression of constants
 */
int64_t constant(node_t *node) {
    if (node->type == NUM) {
        num_node_t *num_node = (num_node_t *) node;
        return num_node->value;
    }
    binary_node_t *bin_node = (binary_node_t *) node;
    char op = bin_node->op;
    int64_t left = constant(bin_node->left);
    int64_t right = constant(bin_node->right);
    return const_operate(op, left, right);
}

/*
 * Returns true if a node represents an expression of constants,
 * Otherwise, generates the asm code required to compute and
 * push the evaluation of the input node to the top of the stack
 * and returns false.
 * 
 * Optimizations include replacing multiplication by a power of 2
 * with a bit shift, and replacing expressions of constants with
 * the value they represent.
 */
bool optimize(node_t *node, register_data *data) {
    if (node->type == NUM) {
        return true;
    }
    else if (node->type == VAR) {
        var_node_t *var_node = (var_node_t *) node;
        push_var(var_node->name, data);
    }
    else if (node->type == BINARY_OP) {
        binary_node_t *bin_node = (binary_node_t *) node;
        bool swap = false;
        bool left = optimize(bin_node->left, data);
        bool right = optimize(bin_node->right, data);
        if (left && right) {
            if (bin_node->op != '>' && bin_node->op != '<' && bin_node->op != '=') {
                return true;
            }
            push_val(constant(bin_node->left), data);
            push_val(constant(bin_node->right), data);
        }
        else if (left) {
            push_val(constant(bin_node->left), data);

            /*
             * In general, the value of the left node is pushed to the virtual stack
             * before the value of the right node, but if the left node is an
             * expression of constants and the right is not, the result of evaluating
             * the right node would have gotten pushed first. This must be taken into
             * account when evaluating the binary operation between the left and right 
             * nodes.
             */
            swap = true;
        }
        else if (right) {
            int64_t val = constant(bin_node->right);
            bool shifted = false;

            // Replaces multiplication by a power of 2 with a bit shift.
            if (val != 0 && bin_node->op == '*') {
                int8_t shift = const_shift(val);
                if (shift != __SCHAR_MAX__) {
                    bin_node->op = 's';
                    if (shift <= 0 && val < 0) {
                        shift *= -1;
                        if (data->stack_index > data->num_stack_regs) {
                            printf("    neg (%%rsp)\n");
                        }
                        else {
                            printf("    neg %s\n",
                                   data->virtual_stack[data->stack_index - 1]);
                        }
                    }
                    printf("    mov $%" PRId8 ", %%cl\n", shift);
                    shifted = true;
                }
            }

            if (!shifted) {
                push_val(val, data);
            }
        }

        // Evaluates the binary operation on the left and right nodes.
        asm_operate(bin_node, data, swap);
    }
    return false;
}

/*
 * Recursively traverses the parse tree and generates asm code for
 * each node, directing compilation of mathematical expressions to
 * the optimize function to enable O(n) compilation.
 */
bool compile_recursive(node_t *node, register_data *data) {
    if (node->type == NUM) {
        num_node_t *num_node = (num_node_t *) node;
        push_val(num_node->value, data);
        assert(data->stack_index == 1); // should store in rdi
    }
    else if (node->type == PRINT) {
        print_node_t *print_node = (print_node_t *) node;
        compile_recursive(print_node->expr, data);
        printf("    call print_int\n");
        (data->stack_index)--;
        assert(data->stack_index == 0); // should have popped from rdi
    }
    else if (node->type == SEQUENCE) {
        sequence_node_t *seq_node = (sequence_node_t *) node;
        for (size_t i = 0; i < seq_node->statement_count; i++) {
            compile_recursive(seq_node->statements[i], data);
        }
    }
    else if (node->type == BINARY_OP) {
        assert(data->stack_index == 0);
        if (optimize(node, data)) {
            push_val(constant(node), data);
        }
    }
    else if (node->type == VAR) {
        var_node_t *var_node = (var_node_t *) node;
        push_var(var_node->name, data);
        // vars that are part of an expression should be handled by optimize
        assert(data->stack_index == 1);
    }
    else if (node->type == LET) {
        let_node_t *let_node = (let_node_t *) node;
        compile_recursive(let_node->value, data); // result should be in rdi
        int16_t var_index = find_var(let_node->var, data);
        if (var_index >= 0) {
            pop(data->var_regs[var_index], data);
        }
        else {
            printf("    movq %%rdi, %" PRId16 "(%%rbp)\n", var_index);
            (data->stack_index)--;
        }
        assert(data->stack_index == 0);
    }
    else if (node->type == IF) {
        if_node_t *if_node = (if_node_t *) node;
        compile_recursive(if_node->condition, data);
        char op = ((binary_node_t *) (if_node->condition))->op;
        uint16_t frame_counter = counter++;
        if (op == '=') {
            printf("    jne .IF%u\n", frame_counter);
        }
        else if (op == '>') {
            printf("    jle .IF%u\n", frame_counter);
        }
        else if (op == '<') {
            printf("    jge .IF%u\n", frame_counter);
        }
        compile_recursive(if_node->if_branch, data);
        if (if_node->else_branch != NULL) {
            printf("    jmp .IF%u\n", counter);
            printf(".IF%u:\n", frame_counter);
            frame_counter = counter++;
            compile_recursive(if_node->else_branch, data);
        }
        printf(".IF%u:\n", frame_counter);
    }
    else if (node->type == WHILE) {
        while_node_t *while_node = (while_node_t *) node;
        uint16_t frame_counter = counter++;
        printf("    jmp .START%u\n", frame_counter);
        printf(".BODY%u:\n", frame_counter);
        compile_recursive(while_node->body, data);
        printf(".START%u:\n", frame_counter);
        compile_recursive(while_node->condition, data);
        char op = ((binary_node_t *) (while_node->condition))->op;
        if (op == '=') {
            printf("    je .BODY%u\n", frame_counter);
        }
        else if (op == '>') {
            printf("    jg .BODY%u\n", frame_counter);
        }
        else if (op == '<') {
            printf("    jl .BODY%u\n", frame_counter);
        }
    }
    else {
        return false;
    }
    return true;
}

/*
 * Fills counts with the number of occurrences of each variable.
 */
void count_vars(node_t *node, uint8_t *counts) {
    if (node->type == PRINT) {
        print_node_t *print_node = (print_node_t *) node;
        count_vars(print_node->expr, counts);
    }
    else if (node->type == SEQUENCE) {
        sequence_node_t *seq_node = (sequence_node_t *) node;
        for (size_t i = 0; i < seq_node->statement_count; i++) {
            count_vars(seq_node->statements[i], counts);
        }
    }
    else if (node->type == BINARY_OP) {
        binary_node_t *bin_node = (binary_node_t *) node;
        count_vars(bin_node->left, counts);
        count_vars(bin_node->right, counts);
    }
    else if (node->type == VAR) {
        var_node_t *var_node = (var_node_t *) node;
        uint8_t idx = var_node->name - 'A';
        counts[idx]++;
    }
    else if (node->type == LET) {
        let_node_t *let_node = (let_node_t *) node;
        count_vars(let_node->value, counts);
        uint8_t idx = let_node->var - 'A';
        counts[idx]++;
    }
    else if (node->type == IF) {
        if_node_t *if_node = (if_node_t *) node;
        count_vars(if_node->condition, counts);
        count_vars(if_node->if_branch, counts);
        if (if_node->else_branch != NULL) {
            count_vars(if_node->else_branch, counts);
        }
    }
    else if (node->type == WHILE) {
        while_node_t *while_node = (while_node_t *) node;
        count_vars(while_node->body, counts);
        count_vars(while_node->condition, counts);
    }
}

/*
 * Fills top_vars with the top 5 variables by frequency, padded by 0s
 */
void choose_vars(uint8_t *counts, uint8_t *top_counts, char *top_vars) {
    for (uint8_t i = 0; i < 26; i++) {
        uint8_t replace_idx = 0;
        for (uint8_t j = 1; j < 5; j++) {
            if (top_counts[j] < top_counts[replace_idx]) {
                replace_idx = j;
            }
        }
        uint8_t count = counts[i];
        if (count > top_counts[replace_idx]) {
            top_counts[replace_idx] = count;
            top_vars[replace_idx] = 'A' + i;
        }
    }
}

/*
 * Initializes a register_data struct with the data needed to store variables
 * and temporary computations in registers while they are available.  
 */
bool compile_ast(node_t *node) {

    /*
     * Initializing arrays of caller-save and callee-save registers that can
     * be used for temporary storage and variable storage respectively.
     */ 
    char *init_calc_regs[] = {"%rdi", "%rsi", "%r8", "%r9", "%r10", "%r11"};
    char *init_var_regs[] = {"%rbx", "%r12", "%r13", "%r14", "%r15"};
    uint8_t len_init_calc_regs = sizeof(init_calc_regs) / sizeof(init_calc_regs[0]);
    uint8_t len_init_var_regs = sizeof(init_var_regs) / sizeof(init_var_regs[0]);

    /*
     * Initializing data structures used for identifying the most frequently
     * appearing variables in the input program.
     */
    uint8_t *counts = (uint8_t *) calloc(26, sizeof(uint8_t));
    uint8_t *top_counts = (uint8_t *) calloc(len_init_var_regs, sizeof(uint8_t));
    char *top_vars = (char *) calloc(len_init_var_regs, sizeof(char));
    count_vars(node, counts);
    choose_vars(counts, top_counts, top_vars);

    // Counts the number of variables for which to reserve registers.
    uint8_t num_vars = 0;
    for (; num_vars < len_init_var_regs && top_vars[num_vars] != 0; num_vars++)
        ;

    // Computes the number of registers to reserve for temporary storage.
    uint8_t num_stack_regs = len_init_calc_regs + len_init_var_regs - num_vars;

    char **virtual_stack = (char **) calloc(num_stack_regs, sizeof(char *));
    char **var_regs = (char **) calloc(num_vars, sizeof(char *));

    /*
     * Fills the virtual stack with all caller-save registers and any callee-
     * save registers that are not needed for variable storage.
     */
    uint8_t i = 0;
    for (i = 0; i < len_init_calc_regs; i++) {
        virtual_stack[i] = init_calc_regs[i];
    }
    for (i = 0; i < len_init_var_regs - num_vars; i++) {
        virtual_stack[i + len_init_calc_regs] = init_var_regs[i];
    }

    /*
     * Fills the array of variable registers with the remaining callee-save
     * registers.
     */
    for (; i < len_init_var_regs; i++) {
        var_regs[i - (len_init_var_regs - num_vars)] = init_var_regs[i];
    }

    register_data data;
    data.stack_index = 0;
    data.virtual_stack = virtual_stack;
    data.var_regs = var_regs;
    data.num_stack_regs = num_stack_regs;
    data.num_vars = num_vars;
    data.vars = top_vars;

    bool result = compile_recursive(node, &data);

    free(virtual_stack);
    free(var_regs);
    free(counts);
    free(top_counts);
    free(top_vars);

    return result;
}
