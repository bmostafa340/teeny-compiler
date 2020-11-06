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
void callee_save(register_data *data, uint8_t len_init_var_regs, char **init_var_regs);
void callee_recall(register_data *data, uint8_t len_init_var_regs, char **init_var_regs);
bool compile_ast(node_t *node);

// Helper for push_var used to get info about where a variable is stored
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

// Pushes a variable's value to the "stack"
void push_var(char name, register_data *data) {
    int16_t var_index = find_var(name, data);
    if (var_index >= 0) {
        if (data->stack_index >= data->num_stack_regs) {
            printf("    push %s\n", data->var_regs[var_index]);
        }
        else {
            printf("    mov %s, %s\n", data->var_regs[var_index],
                   data->virtual_stack[data->stack_index]);
        }
    }
    else {
        // fprintf(stderr, "%" PRId16 "\n", var_index);
        if (data->stack_index >= data->num_stack_regs) {
            printf("    push %" PRId16 "(%%rbp)\n", var_index);
        }
        else {
            printf("    mov %" PRId16 "(%%rbp), %s\n", var_index,
                   data->virtual_stack[data->stack_index]);
        }
    }
    (data->stack_index)++;
}

// Pushes a constant to the "stack"
void push_val(int64_t val, register_data *data) {
    if (data->stack_index >= data->num_stack_regs) {
        printf("    push $%" PRId64 "\n", val);
    }
    else {
        printf("    mov $%" PRId64 ", %s\n", val, data->virtual_stack[data->stack_index]);
    }
    (data->stack_index)++;
}

// Pushes a register's value to the "stack"
void push_reg(char *reg, register_data *data) {
    if (data->stack_index >= data->num_stack_regs) {
        printf("    push %s\n", reg);
    }
    else {
        printf("    mov %s, %s\n", reg, data->virtual_stack[data->stack_index]);
    }
    (data->stack_index)++;
}

// pops the "stack" to a given register
void pop(char *reg, register_data *data) {
    if (data->stack_index > data->num_stack_regs) {
        printf("    pop %s\n", reg);
    }
    else {
        printf("    mov %s, %s\n", data->virtual_stack[data->stack_index - 1], reg);
    }
    (data->stack_index)--;
}

// computes k where value = 2^k
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

// operates on constants
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

// Prints assembly to perform perform a binary_node operation
// result should be on top of the "stack"
// preconditions:
// if the operation is not a shift:
//     the result of the right node is on top of the "stack"
//     the result of the left node is immediately below
// if the operation is a shift:
//     the result of the right node is in %cl
//     the result of the left node is on top of the "stack"
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

// Computes the constant represented by node
// precondition:
//    node is a NUM or an expression of constants
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

// If a node is an expression of constants:
//    returns true
// Else:
//    Puts the result of evaluating the node on top of the "stack"
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
            swap = true;
        }
        else if (right) {
            int64_t val = constant(bin_node->right);
            bool shifted = false;
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
        asm_operate(bin_node, data, swap);
    }
    return false;
}

// analogous to the original compile_ast function
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
        assert(data->stack_index ==
               1); // vars that are part of an expression should be handled by optimize
    }
    else if (node->type == LET) {
        let_node_t *let_node = (let_node_t *) node;
        compile_recursive(let_node->value, data); // result should be in rdi
        int16_t var_index = find_var(let_node->var, data);
        if (var_index >= 0) {
            pop(data->var_regs[var_index], data);
        }
        else {
            printf("    mov %%rdi, %" PRId16 "(%%rbp)\n", var_index);
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

// Fills counts with number of occurrences of each variable
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

// Fills top_vars with the top 5 variables by frequency, padded by 0s
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

void callee_save(register_data *data, uint8_t len_init_var_regs, char **init_var_regs) {
    char illegal[5] = {0};
    for (int8_t i = 0; i < len_init_var_regs; i++) {
        if (i < data->num_vars) {
            illegal[i] = data->vars[i];
            // fprintf(stderr, "%c\n", data->vars[i]);
            int16_t j = -8 * (data->vars[i] - 'A' + 1);
            printf("    mov %s, %" PRId16 "(%%rbp)\n", data->var_regs[i], j);
        }
        else {
            int8_t c = 0;
            for (int8_t c1 = 'A'; c1 <= 'Z'; c1++) {
                for (int8_t j = 0; j < 5; j++) {
                    if (c1 == illegal[j]) {
                        break;
                    }
                    if (j == 4) {
                        c = c1;
                    }
                }
                if (c != 0) {
                    break;
                }
            }
            illegal[i] = c;
            int16_t j = -8 * (c - 'A' + 1);
            // fprintf(stderr, init_var_regs[i - data->num_vars]);
            printf("    mov %s, %" PRId16 "(%%rbp)\n", init_var_regs[i - data->num_vars],
                   j);
        }
    }
}

void callee_recall(register_data *data, uint8_t len_init_var_regs, char **init_var_regs) {
    char illegal[5] = {0};
    for (int8_t i = 0; i < len_init_var_regs; i++) {
        if (i < data->num_vars) {
            illegal[i] = data->vars[i];
            int16_t j = -8 * (data->vars[i] - 'A' + 1);
            printf("    mov %" PRId16 "(%%rbp), %s\n", j, data->var_regs[i]);
        }
        else {
            int8_t c = 0;
            for (int8_t c1 = 'A'; c1 <= 'Z'; c1++) {
                for (int8_t j = 0; j < 5; j++) {
                    if (c1 == illegal[j]) {
                        break;
                    }
                    if (j == 4) {
                        c = c1;
                    }
                }
                if (c != 0) {
                    break;
                }
            }
            illegal[i] = c;
            int16_t j = -8 * (c - 'A' + 1);
            printf("    mov %" PRId16 "(%%rbp), %s\n", j,
                   init_var_regs[i - data->num_vars]);
        }
    }
}

// Sets up a register_data struct that includes the registers used to store
// the more frequently appearing variables, as well as the registers used in
// combination with the stack.
bool compile_ast(node_t *node) {
    char *init_calc_regs[] = {"%rdi", "%rsi", "%r8", "%r9", "%r10", "%r11"};
    char *init_var_regs[] = {"%rbx", "%r12", "%r13", "%r14", "%r15"};
    uint8_t len_init_calc_regs = sizeof(init_calc_regs) / sizeof(init_calc_regs[0]);
    uint8_t len_init_var_regs = sizeof(init_var_regs) / sizeof(init_var_regs[0]);

    uint8_t *counts = (uint8_t *) calloc(26, sizeof(uint8_t));
    uint8_t *top_counts = (uint8_t *) calloc(len_init_var_regs, sizeof(uint8_t));
    char *top_vars = (char *) calloc(len_init_var_regs, sizeof(char));
    count_vars(node, counts);
    choose_vars(counts, top_counts, top_vars);

    uint8_t num_vars = 0;
    for (; num_vars < len_init_var_regs && top_vars[num_vars] != 0; num_vars++)
        ;

    uint8_t num_stack_regs = len_init_calc_regs + len_init_var_regs - num_vars;

    char **virtual_stack = (char **) calloc(num_stack_regs, sizeof(char *));
    char **var_regs = (char **) calloc(num_vars, sizeof(char *));

    uint8_t i = 0;
    for (i = 0; i < len_init_calc_regs; i++) {
        virtual_stack[i] = init_calc_regs[i];
    }
    for (i = 0; i < len_init_var_regs - num_vars; i++) {
        virtual_stack[i + len_init_calc_regs] = init_var_regs[i];
    }
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

    callee_save(&data, len_init_var_regs, init_var_regs);

    bool result = compile_recursive(node, &data);

    callee_recall(&data, len_init_var_regs, init_var_regs);

    free(virtual_stack);
    free(var_regs);
    free(counts);
    free(top_counts);
    free(top_vars);

    return result;
}

/*
bool compile_recursive(node_t *node);

uint16_t counter = 0;

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

void asm_operate(binary_node_t *bin_node) {
    if (bin_node->op == '+') {
        printf("    addq %%r10, %%rax\n");
    }
    else if (bin_node->op == '*') {
        printf("    imulq %%r10, %%rax\n");
    }
    else if (bin_node->op == 's') {
        printf("    sal %%cl, %%rax\n");
    }
    else if (bin_node->op == '-') {
        printf("    subq %%r10, %%rax\n");
    }
    else if (bin_node->op == '/') {
        printf("    cqto\n");
        printf("    idivq %%r10\n");
    }
    else {
        printf("    cmp %%r10, %%rax\n");
    }
}

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

bool optimize(node_t *node) {
    if (node->type == NUM) {
        return true;
    }
    else if (node->type == BINARY_OP) {
        binary_node_t *bin_node = (binary_node_t *) node;
        bool right = optimize(bin_node->right);
        if (!right) {
            printf("    push %%rax\n");
        }
        bool left = optimize(bin_node->left);
        if (!right) {
            printf("    pop %%r10\n");
        }
        if (left && right) {
            if (bin_node->op != '>' && bin_node->op != '<' && bin_node->op != '=') {
                return true;
            }
            int64_t v_left = constant(bin_node->left);
            int64_t v_right = constant(bin_node->right);
            printf("    mov $%" PRId64 ", %%rax\n", v_left);
            printf("    mov $%" PRId64 ", %%r10\n", v_right);
        }
        else if (left) {
            int64_t val = constant(bin_node->left);
            printf("    mov $%" PRId64 ", %%rax\n", val);
        }
        else if (right) {
            int64_t val = constant(bin_node->right);
            bool shifted = false;
            if (bin_node->op == '*') {
                int8_t shift = const_shift(val);
                if (shift != __SCHAR_MAX__) {
                    bin_node->op = 's';
                    if (shift <= 0 && val < 0) {
                        shift *= -1;
                        printf("    neg %%rax\n");
                    }
                    printf("    mov $%" PRId8 ", %%cl\n", shift);
                    shifted = true;
                }
            }
            if (!shifted) {
                printf("    mov $%" PRId64 ", %%r10\n", val);
            }
        }
        asm_operate(bin_node);
    }
    else {
        compile_recursive(node);
    }
    return false;
}

bool compile_recursive(node_t *node) {
    if (node->type == NUM) {
        num_node_t *num_node = (num_node_t *) node;
        printf("    mov $%" PRId64 ", %%rax\n", num_node->value);
    }
    else if (node->type == PRINT) {
        print_node_t *print_node = (print_node_t *) node;
        compile_recursive(print_node->expr);
        printf("    mov %%rax, %%rdi\n");
        printf("    call print_int\n");
    }
    else if (node->type == SEQUENCE) {
        sequence_node_t *seq_node = (sequence_node_t *) node;
        for (size_t i = 0; i < seq_node->statement_count; i++) {
            compile_recursive(seq_node->statements[i]);
        }
    }
    else if (node->type == BINARY_OP) {
        if (optimize(node)) {
            int64_t val = constant(node);
            printf("    mov $%" PRId64 ", %%rax\n", val);
        }
    }
    else if (node->type == VAR) {
        var_node_t *var_node = (var_node_t *) node;
        uint8_t addr = var_node->name - 'A' + 1;
        printf("    mov $-%u, %%r10\n", addr);
        printf("    mov (%%rbp, %%r10, 8), %%rax\n");
    }
    else if (node->type == LET) {
        let_node_t *let_node = (let_node_t *) node;
        compile_recursive(let_node->value);
        uint8_t addr = let_node->var - 'A' + 1;
        printf("    mov $-%u, %%r10\n", addr);
        printf("    mov %%rax, (%%rbp, %%r10, 8)\n");
    }
    else if (node->type == IF) {
        if_node_t *if_node = (if_node_t *) node;
        compile_recursive(if_node->condition);
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
        compile_recursive(if_node->if_branch);
        if (if_node->else_branch != NULL) {
            printf("    jmp .IF%u\n", counter);
            printf(".IF%u:\n", frame_counter);
            frame_counter = counter++;
            compile_recursive(if_node->else_branch);
        }
        printf(".IF%u:\n", frame_counter);
    }
    else if (node->type == WHILE) {
        while_node_t *while_node = (while_node_t *) node;
        uint16_t frame_counter = counter++;
        printf("    jmp .START%u\n", frame_counter);
        printf(".BODY%u:\n", frame_counter);
        compile_recursive(while_node->body);
        printf(".START%u:\n", frame_counter);
        compile_recursive(while_node->condition);
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

bool compile_ast(node_t *node) {
    return compile_recursive(node);
}
*/
