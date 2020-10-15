#include "compile.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

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
        compile_ast(node);
    }
    return false;
}

bool compile_ast(node_t *node) {
    if (node->type == NUM) {
        num_node_t *num_node = (num_node_t *) node;
        printf("    mov $%" PRId64 ", %%rax\n", num_node->value);
    }
    else if (node->type == PRINT) {
        print_node_t *print_node = (print_node_t *) node;
        compile_ast(print_node->expr);
        printf("    mov %%rax, %%rdi\n");
        printf("    call print_int\n");
    }
    else if (node->type == SEQUENCE) {
        sequence_node_t *seq_node = (sequence_node_t *) node;
        for (size_t i = 0; i < seq_node->statement_count; i++) {
            compile_ast(seq_node->statements[i]);
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
        compile_ast(let_node->value);
        uint8_t addr = let_node->var - 'A' + 1;
        printf("    mov $-%u, %%r10\n", addr);
        printf("    mov %%rax, (%%rbp, %%r10, 8)\n");
    }
    else if (node->type == IF) {
        if_node_t *if_node = (if_node_t *) node;
        compile_ast(if_node->condition);
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
        compile_ast(if_node->if_branch);
        if (if_node->else_branch != NULL) {
            printf("    jmp .IF%u\n", counter);
            printf(".IF%u:\n", frame_counter);
            frame_counter = counter++;
            compile_ast(if_node->else_branch);
        }
        printf(".IF%u:\n", frame_counter);
    }
    else if (node->type == WHILE) {
        while_node_t *while_node = (while_node_t *) node;
        uint16_t frame_counter = counter++;
        printf("    jmp .START%u\n", frame_counter);
        printf(".BODY%u:\n", frame_counter);
        compile_ast(while_node->body);
        printf(".START%u:\n", frame_counter);
        compile_ast(while_node->condition);
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
bool compile_ast(node_t *node) {
    if (node->type == NUM) {
        num_node_t *num_node = (num_node_t *) node;
        printf("    mov $%" PRId64 ", %%rax\n", num_node->value);
    }
    else if (node->type == PRINT) {
        print_node_t *print_node = (print_node_t *) node;
        compile_ast(print_node->expr);
        printf("    mov %%rax, %%rdi\n");
        printf("    call print_int\n");
    }
    else if (node->type == SEQUENCE) {
        sequence_node_t *seq_node = (sequence_node_t *) node;
        for (size_t i = 0; i < seq_node->statement_count; i++) {
            compile_ast(seq_node->statements[i]);
        }
    }
    else if (node->type == BINARY_OP) {
        binary_node_t *bin_node = (binary_node_t *) node;
        compile_ast(bin_node->right);
        printf("    push %%rax\n");
        compile_ast(bin_node->left);
        printf("    pop %%r10\n");
        if (bin_node->op == '+') {
            printf("    addq %%r10, %%rax\n");
        }
        else if (bin_node->op == '*') {
            printf("    imulq %%r10, %%rax\n");
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
    else if (node->type == VAR) {
        var_node_t *var_node = (var_node_t *) node;
        uint8_t addr = var_node->name - 'A' + 1;
        printf("    mov $-%u, %%r10\n", addr);
        printf("    mov (%%rbp, %%r10, 8), %%rax\n");
    }
    else if (node->type == LET) {
        let_node_t *let_node = (let_node_t *) node;
        compile_ast(let_node->value);
        uint8_t addr = let_node->var - 'A' + 1;
        printf("    mov $-%u, %%r10\n", addr);
        printf("    mov %%rax, (%%rbp, %%r10, 8)\n");
    }
    else if (node->type == IF) {
        if_node_t *if_node = (if_node_t *) node;
        compile_ast(if_node->condition);
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
        compile_ast(if_node->if_branch);
        if (if_node->else_branch != NULL) {
            printf("    jmp .IF%u\n", counter);
            printf(".IF%u:\n", frame_counter);
            frame_counter = counter++;
            compile_ast(if_node->else_branch);
        }
        printf(".IF%u:\n", frame_counter);
    }
    else if (node->type == WHILE) {
        while_node_t *while_node = (while_node_t *) node;
        uint16_t frame_counter = counter++;
        printf("    jmp .START%u\n", frame_counter);
        printf(".BODY%u:\n", frame_counter);
        compile_ast(while_node->body);
        printf(".START%u:\n", frame_counter);
        compile_ast(while_node->condition);
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
*/

/*
typedef struct {
    bool is_constant;
    int64_t constant;
} opt_data_t;

opt_data_t init_opt_data() {
    opt_data_t result;
    result.is_constant = false;
    return result;
}
*/

/*
bool is_constant(node_t *node) {
    if (node->type == NUM) {
        return true;
    }
    binary_node_t *bin_node = (binary_node_t *) node;
    if (is_constant(bin_node->left) && is_constant(bin_node->right)) {
        return true;
    }
    return false;
}
*/

/*
 *
 * void left_const(binary_node_t *node) {

}

bool optimizations(binary_node_t *node) {
    if (is_constant(node)) {
        return true;
    }
    bool is_const_left = is_constant(node->left);
    bool is_const_right = is_constant(node->right);
    if (is_const_left) {
        int64_t const_left = constant(node->left);
    }
    else if (is_const_right) {
        int64_t const_right = constant(node->right);
    }
    else {
        compile_ast(node->right);
        printf("    push %%rax\n");
        compile_ast(node->left);
        printf("    pop %%r10\n");
        if (node->op == '+') {
            printf("    addq %%r10, %%rax\n");
        }
        else if (node->op == '*') {
            printf("    imulq %%r10, %%rax\n");
        }
        else if (node->op == '-') {
            printf("    subq %%r10, %%rax\n");
        }
        else if (node->op == '/') {
            printf("    cqto\n");
            printf("    idivq %%r10\n");
        }
    }
}

opt_data_t optimizations2(binary_node_t *node) {
    opt_data_t opt_data = init_opt_data();
    if (node->type == NUM) {
        num_node_t *num_node = (num_node_t *) node;
        opt_data.is_constant = true;
        opt_data.constant = num_node->value;
    }
    else if (node->type == VAR) {
        var_node_t *var_node = (var_node_t *) node;
        uint8_t addr = var_node->name - 'A' + 1;
        printf("    mov $-%u, %%r10\n", addr);
        printf("    mov (%%rbp, %%r10, 8), %%rax\n");
    }
    else if (node->type == BINARY_OP) {
        binary_node_t *bin_node = (binary_node_t *) node;
        opt_data_t right = optimizations(bin_node->right);
        if (!right.is_constant) {
            printf("    push %%rax\n");
        }
        opt_data_t left = optimizations(bin_node->left);
        if (!left.is_constant && !right.is_constant) {
            printf("    pop %%r10\n");
        }
        if (right.is_constant && left.is_constant) {
            opt_data.is_constant = true;
            opt_data.constant = const_operate(bin_node->op, left.constant,
right.constant);
        }
        else if (bin_node->op == '+') {
            printf("    addq %%r10, %%rax\n");
        }
        else if (bin_node->op == '*') {
            if (right.is_constant) {
                int8_t value = right.constant;
                uint8_t shift = 0;
                while (value % 2 == 0) {
                    value /= 2;
                    shift += 1;
                }
                if (value == -1) {
                    printf("    neg %%rax\n");
                    printf("    sal $%u, %%rax\n", shift);
                }
                else if (value == 1) {
                    printf("    sal $%u, %%rax\n", shift);
                }
                else {
                    printf("    imulq $" PRId64 "%%rax\n", right.constant);
                }
            }
            printf("    imulq %%r10, %%rax\n");
        }
        else if (bin_node->op == '-') {
            printf("    subq %%r10, %%rax\n");
        }
        else if (bin_node->op == '/') {
            printf("    cqto\n");
            printf("    idivq %%r10\n");
        }
        else if (right.is_constant) {
            int8_t value = right.constant;
            uint8_t shift = 0;
            while (value % 2 == 0) {
                value /= 2;
                shift += 1;
            }
            if (value == -1) {
                printf("    neg %%rax\n");
                printf("    sal $%u, %%rax\n", shift);
            }
            if (value == 1) {
                printf("    sal $%u, %%rax\n", shift);
            }
        }
        compile_ast(bin_node->right);
        printf("    push %%rax\n");
        compile_ast(bin_node->left);
        printf("    pop %%r10\n");
    }
    int64_t left = optimizations(node->left);
    int64_t right = optimizations(node->right);
}
*/
