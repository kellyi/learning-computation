%include 'functions.asm'

SECTION .data
one db "hello!", 0
two db "world!", 0

SECTION .text
global _start

_start:
  mov eax, one
  call sprint

  mov eax, two
  call sprint

  call quit
