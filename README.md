# teeny-compiler
A TeenyBASIC compiler, extended from Caltech CS 24 project 2. Caltech students who plan to take CS 24 should not look further by the Honor Code.

TeenyBASIC is a simplified version of BASIC. It is discussed in more detail here: https://com.puter.systems/20fa/projects/02. 
The parser, test suite, and Makefile were provided as starter code. My contribution to the project is the compile.c file and some of the compiler.c file.

# Usage:

Data types are limited to 64 bit ints.

The only builtin arithmetic operations are +, -, *, /.

Variable names are limited to the characters A through Z.

Only comparisons of equality (=), greater than (>), and less than (<) are provided.

It is limited to the following types of statements.

Print the value of expression to stdout

	PRINT expression

Store the value represented by expression in variable

	LET variable = expression

Execute statement1, statement2, statement3, ...

	statement1

	statement2

	statement3

	...

If condition is true, execute true_statements

	IF condition

		true_statements
  
	END IF

If condition is true, execute true_statements. Else, execute false_statements.

	IF condition

		true_statements
  
	ELSE

		false_statements
  
	END IF

Execute statements until condition becomes false.

	WHILE condition

		statements
  
	END WHILE

The provided Makefile can be used to produce the compiler binary. 

First, ensure that the CC and ASM parameters in the Makefile are changed to reflect the C compiler on your machine.

Then install make, and type "make bin/compiler". To use the binary, create a TeenyBASIC program, then type ./compiler <path to program> to print the equivalent assembly code to stdout. "make compile1", "make compile2", ... "make compile7" compile a selection of provided TeenyBASIC programs and ensure the correctness of the output code. "make opt1" and "make opt2" test the code on TeenyBASIC programs geared to benefit from certain optimizations in order to ensure that the compiler successfully performs said opimizations.

# Implementation Highlights:

The compiler generates assembly code that is optimized to replace multiplication by powers of 2 with bit shifts and expressions of constants with the value they represent. It is also optimized to use callee-save registers as opposed to the stack to store the most frequently appearing variables, as well as using caller-save and the remaining callee-save registers to store temporary results involved in computing large arithemetic expressions. Since multiple register operations can occur within a CPU cycle, whereas stack operations generally take more than a CPU cycle, using registers where possible confers a significant speed improvement.

The ability to detect and use registers while they are available is facilitated by extending the stack with a "virtual stack," which is conceptually like using a list of registers as the first few indices of the stack. Helper functions that manage pushes and pops involving this virtual stack make for a relatively clean implementation.

The compiler is also optimized to run in O(n) time on the size of the parse tree.
