#include <unistd.h>

#include "rM-input-devices.h"

int main(int argc, char *argv) {
  find_rm_input_devices(1);
  while (1) { pause(); }
}
