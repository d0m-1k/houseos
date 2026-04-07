ENTRY(_start)

PHDRS
{
  interp PT_INTERP FLAGS(4);
  text   PT_LOAD   FLAGS(5);
  data   PT_LOAD   FLAGS(6);
}

SECTIONS
{
  . = 0x00400000;

  .interp : {
    *(.interp)
  } :interp :text

  .text : {
    *(.text*)
  } :text

  .rodata : {
    *(.rodata*)
  } :text

  .data : {
    *(.data*)
    *(.got*)
    *(.got.plt*)
  } :data

  .bss : {
    *(.bss*)
    *(COMMON)
  } :data
}
