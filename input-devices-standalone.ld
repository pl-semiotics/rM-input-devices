SECTIONS
{
  .uinput_ko : {
    uinput_ko_begin = . ;
    build/uinput.bin(.data)
    uinput_ko_end = . ;
  }
}
