ENTRY(_start)

PHDRS
{
  interp  PT_INTERP  FLAGS(4);
  text    PT_LOAD    FLAGS(5);
  data    PT_LOAD    FLAGS(6);
  dynamic PT_DYNAMIC FLAGS(6);
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

  . = ALIGN(0x1000);

  .dynamic : {
    *(.dynamic)
  } :data :dynamic

  .dynsym : {
    *(.dynsym)
  } :data

  .dynstr : {
    *(.dynstr)
  } :data

  .hash : {
    *(.hash)
  } :data

  .gnu.hash : {
    *(.gnu.hash)
  } :data

  .rel.dyn : {
    *(.rel.dyn)
  } :data

  .rel.plt : {
    *(.rel.plt)
  } :data

  .got : {
    *(.got)
    *(.got.plt)
  } :data

  .data : {
    *(.data*)
  } :data

  .bss : {
    *(.bss*)
    *(COMMON)
  } :data
}
