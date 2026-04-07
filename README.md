# cmm-standart-26
this utilit for sphinx c-- include typedef and C defines + sphinx c-- compiler and libs

in this toolChain 3 utilit:
t26 - difficult typedef 
example:

typedef unsigned int uint32_t;

for use in terminal:

t26 <file.h|file.h--|file.c-->

# cmm26 - utilit for .c-- files, this utilit include typedef and C defines
example:

typedef float myFloat;

typedef struct {

}my struct;

#define myDef {


}


for use in terminal:

# cmm26 <make.ini> <file.c-->


#hmm26 - utilit for .h/.h-- files, this utilit like cmm26 but for include files

for use in terminal:
# hmm26 <include file.h/h-->


# path notes 01:
in t26 include C calling convension, this is:
__stdcall
__cdecl

example:

extern int __stdcall intFunc();

# c--_26

c--_26 is include 3 utilit and auto enable this utilit
