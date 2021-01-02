#include "rM-input-devices.h"

#include <pthread.h>


struct fd_list {
  int fd;
  struct fd_list *next;
};

#define N_SLOTS 32
struct wacom_data {
  pthread_mutex_t mutex;
  int pen_down; int touch_down;
  int abs_x; int abs_y; int abs_pressure;
  int drop_until_syn;
  void *userdata;
  uint coord_kind;
};
struct touch_data {
  pthread_mutex_t mutex;
  int slots[N_SLOTS]; /* keep track of the tracking id for each slot */
  int abs_x[N_SLOTS];
  int abs_y[N_SLOTS];
  int current_slot;
  /* the kernel currently uses (mt->trkid++ & TRKID_MAX) to get a new
   * tracking id, so we just stay a few thousand ids ahead of the
   * kernel */
#define TRKID_MAX 0xffff
#define KERN_TRKID_OFFSET 4096
  uint next_trkid;
  int drop_until_syn;
  void *userdata;
  uint coord_kind;
};
struct key_data {
  pthread_mutex_t mutex;
  void *userdata;
};

struct rM_input_devices_priv {
  struct fd_list* extra_wacom_fds;
  struct fd_list* extra_touch_fds;
  struct fd_list* extra_key_fds;
  handle_wacom_event_t hwe;
  handle_touch_event_t hte;
  handle_key_event_t hke;
  pthread_mutex_t input_thread_mutex;
  int input_thread_running;
  pthread_t input_thread;
  struct wacom_data wd;
  struct touch_data td;
  struct key_data kd;
};
