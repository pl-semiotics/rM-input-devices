#ifndef RM_INPUT_DEVICES_H_
#define RM_INPUT_DEVICES_H_

#include <sys/types.h>

/* file descriptors */
struct rM_input_devices {
  int digitizer;
  int touch;
  int kbd;
  struct rM_input_devices_priv *priv;
};

struct rM_input_devices find_rm_input_devices(int create_if_missing);

#define RM_X 0x1
#define RM_Y 0x2
#define RM_PRESSURE 0x4

#define RM_COORD_EVDEVICE 0x1
#define RM_COORD_DISPLAY 0x2
struct rM_coord {
  uint coord_kind;
  uint x;
  uint y;
};

/* needed for an on_*_event, and for submit_touch_* */
int enable_input_event_listening(struct rM_input_devices *ds);

/* the various handle_* fns should be idempotent */

#define WHICH_WACOM_PEN 0x1
#define WHICH_WACOM_TOUCH 0x2
#define WHICH_WACOM_X 0x4
#define WHICH_WACOM_Y 0x8
#define WHICH_WACOM_PRESSURE 0x10
int submit_wacom_event(struct rM_input_devices *ds,
                       int pen_down, int touch_down,
                       struct rM_coord coord, int abs_pressure,
                       uint which);
typedef void (*handle_wacom_event_t)(void *, int pen_down, int touch_down,
                                     int abs_x, int abs_y, int abs_pressure);
int on_wacom_event(struct rM_input_devices *ds, uint coord_kind,
                   handle_wacom_event_t handle, void *);

#define WHICH_TOUCH_X 1
#define WHICH_TOUCH_Y 2
int touch_begin_contact(struct rM_input_devices *ds);
int submit_touch_contact(struct rM_input_devices *ds, int c,
                         struct rM_coord coord, int which);
int touch_end_contact(struct rM_input_devices *ds, int c);
typedef void (*handle_touch_event_t)(void *, int c, int abs_x, int abs_y);
int on_touch_event(struct rM_input_devices *ds, uint coord_kind,
                   handle_touch_event_t handle, void *);

int submit_key_event(struct rM_input_devices *ds, int key, int down);
typedef void (*handle_key_event_t)(void *, int key, int down);
int on_key_event(struct rM_input_devices *ds, handle_key_event_t handle, void *);

#endif /* RM_INPUT_DEVICES_H_ */
