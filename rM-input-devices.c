#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/epoll.h>

#include <linux/uinput.h>
#include <libudev.h>

#include "private.h"

struct input_device {
  uint *propbits;
  uint *evbits;
  uint *keybits;
  struct uinput_abs_setup *abs;
  struct uinput_setup setup;
  char *udev_prop_filter;
  struct fd_list *fds;
};

uint digitizer_evbits[] = {EV_KEY,EV_ABS,0};
uint digitizer_keybits[] = {
  BTN_TOOL_PEN,
  BTN_TOOL_RUBBER,
  BTN_TOUCH,
  BTN_STYLUS,
  BTN_STYLUS2,
  0
};
#if REMARKABLE_VERSION < 2
#define DIGITIZER_MAX_X 20967
#define DIGITIZER_MAX_Y 15725
#define TOUCH_MAX_X 767
#define TOUCH_MAX_Y 1023
#else
#define DIGITIZER_MAX_X 20966
#define DIGITIZER_MAX_Y 15725
#define TOUCH_MAX_X 1403
#define TOUCH_MAX_Y 1871
#endif
struct uinput_abs_setup digitizer_abs[] = {
  {
    .code = ABS_X,
    .absinfo = {
      .value = 0,
      .minimum = 0,
      .maximum = DIGITIZER_MAX_X,
    }
  },
  {
    .code = ABS_Y,
    .absinfo = {
      .value = 0,
      .minimum = 0,
      .maximum = DIGITIZER_MAX_Y,
    }
  },
  {
    .code = ABS_PRESSURE,
    .absinfo = {
      .value = 0,
      .minimum = 0,
      .maximum = 4095,
    }
  },
  {
    .code = ABS_DISTANCE,
    .absinfo = {
      .value = 0,
      .minimum = 0,
      .maximum = 255,
    }
  },
  {
    .code = ABS_TILT_X,
    .absinfo = {
      .value = 0,
      .minimum = -9000,
      .maximum = 9000,
    }
  },
  {
    .code = ABS_TILT_Y,
    .absinfo = {
      .value = 0,
      .minimum = -9000,
      .maximum = -9000,
    }
  },
  { 0 },
};
struct input_device digitizer = {
  .propbits = 0,
  .evbits = digitizer_evbits,
  .keybits = digitizer_keybits,
  .abs = digitizer_abs,
  .setup = {
    .id = { .bustype = 0x18, .vendor = 0x56a, .product = 0x0, .version = 0x36 },
    .name = "Wacom I2C Digitizer",
    0
  },
  .udev_prop_filter = "ID_INPUT_TABLET",
};

uint touch_propbits[] = {INPUT_PROP_DIRECT, 0};
uint touch_evbits[] = {EV_KEY,EV_REL,EV_ABS,0};
struct uinput_abs_setup touch_abs[] = {
  {
    .code = ABS_MT_SLOT,
    .absinfo = {
      .value = 0,
      .minimum = 0,
      .maximum = 31,
    }
  },
  {
    .code = ABS_MT_POSITION_X,
    .absinfo = {
      .value = 0,
      .minimum = 0,
      .maximum = TOUCH_MAX_X,
    }
  },
  {
    .code = ABS_MT_POSITION_Y,
    .absinfo = {
      .value = 0,
      .minimum = 0,
      .maximum = TOUCH_MAX_Y,
    }
  },
  {
    .code = ABS_MT_TRACKING_ID,
    .absinfo = {
      .value = 0,
      .minimum = 0,
      .maximum = 65536,
    }
  },
  { 0 },
};
struct input_device touch = {
  .propbits = touch_propbits,
  .evbits = touch_evbits,
  .keybits = 0,
  .abs = touch_abs,
  .setup = {
    .id = { .bustype = 0x0, .vendor = 0x0, .product = 0x0, .version = 0x0 },
    .name = "cyttsp5_mt",
    0
  },
  .udev_prop_filter = "ID_INPUT_TOUCHSCREEN",
};

uint kbd_evbits[] = {EV_KEY,0};
uint kbd_keybits[] = {
  KEY_HOME,
  KEY_LEFT,
  KEY_RIGHT,
  KEY_POWER,
  KEY_WAKEUP,
  0
};
struct input_device kbd = {
  .propbits = 0,
  .evbits = kbd_evbits,
  .keybits = kbd_keybits,
  .abs = 0,
  .setup = {
    .id = { .bustype = 0x19, .vendor = 0x1, .product = 0x1, .version = 0x100 },
    .name = "gpio-keys",
    0
  },
  .udev_prop_filter = "ID_INPUT_KEY",
};

#define mk_not_empty(n, t) \
  static int not_empty_ ## n(t *n) {            \
    t test = {0};                               \
    return !!memcmp(n, &test, sizeof(t));       \
  }
mk_not_empty(abs, struct uinput_abs_setup);
mk_not_empty(dev, struct input_device);

#define UINPUT_KO_ENV "RM_INPUT_DEVICES_UINPUT_KO"
char __attribute__((weak)) uinput_ko_begin = 0;
char __attribute__((weak)) uinput_ko_end = 0;
static void ensure_have_uinput() {
  if (faccessat(AT_FDCWD, "/dev/uinput", F_OK, AT_EACCESS) == 0) { return; }

  char *start = &uinput_ko_begin;
  size_t size = &uinput_ko_end-&uinput_ko_begin;

  char *external = getenv(UINPUT_KO_ENV);
  int fd;
  if (external) {
    fd = open(external, O_RDONLY);
    if (fd < 0) { return; }
    struct stat stat;
    if (fstat(fd, &stat)) { return; }
    start = mmap(NULL, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (start == MAP_FAILED) { return; }
    size = stat.st_size;
  }

  syscall(__NR_init_module, start, size, "");

  if (external) {
    munmap(start, size);
    close(fd);
  }
}

static int create_device(struct input_device *d) {
  ensure_have_uinput();
  int fd = open("/dev/uinput", O_WRONLY|O_NONBLOCK);

  for (uint *bit = d->propbits; bit && *bit; bit++) {
    ioctl(fd, UI_SET_PROPBIT, *bit);
  }

  for (uint *bit = d->evbits; bit && *bit; bit++) {
    ioctl(fd, UI_SET_EVBIT, *bit);
  }

  for (uint *bit = d->keybits; bit && *bit; bit++) {
    ioctl(fd, UI_SET_KEYBIT, *bit);
  }

  for (struct uinput_abs_setup *abs = d->abs; abs && not_empty_abs(abs); abs++) {
    int r = ioctl(fd, UI_ABS_SETUP, abs);
  }

  ioctl(fd, UI_DEV_SETUP, &d->setup);
  ioctl(fd, UI_DEV_CREATE);

  return fd;
}

#define EVDEVICE_PREFIX "/dev/input/event"

static void find_devices(struct input_device devices[], int create_if_missing) {
  struct udev *u = udev_new();
  if (!u) { return; }
  struct udev_enumerate *e = udev_enumerate_new(u);
  udev_enumerate_add_match_subsystem(e, "input");
  for (struct input_device *d = devices; not_empty_dev(d); ++d) {
    d->fds = NULL;
    udev_enumerate_add_match_property(e, d->udev_prop_filter, "1");
  }
  udev_enumerate_scan_devices(e);
  struct udev_list_entry *ds, *de;
  ds = udev_enumerate_get_list_entry(e);
  udev_list_entry_foreach(de, ds) {
    const char *p = udev_list_entry_get_name(de);
    struct udev_device *dev = udev_device_new_from_syspath(u, p);
    const char *devpath = udev_device_get_devnode(dev);
    if (!devpath ||
        strncmp(EVDEVICE_PREFIX, devpath, strlen(EVDEVICE_PREFIX)) ||
        strcmp(EVDEVICE_PREFIX, devpath) >= 0) { continue; }
    int fd = open(devpath, O_RDWR);
    if (fd < 0) { continue; }
#define SIZE(x) ((x ## _MAX+7)/8)
    char props[SIZE(INPUT_PROP)] = {0};
    ioctl(fd, EVIOCGPROP(SIZE(INPUT_PROP)), &props);
    char evbits[SIZE(EV)] = {0};
    ioctl(fd, EVIOCGBIT(0, SIZE(EV)), &evbits);
    char keybits[SIZE(KEY)] = {0};
    ioctl(fd, EVIOCGBIT(EV_KEY, SIZE(KEY)), &keybits);
    char absbits[SIZE(ABS)] = {0};
    ioctl(fd, EVIOCGBIT(EV_ABS, SIZE(ABS)), &absbits);
#define CHECK_BIT(base, n) (base[n/8] & (1<<(n%8)))
    for (struct input_device *d = devices; not_empty_dev(d); ++d) {
      for (uint *bit = d->propbits; bit && *bit; bit++) {
        if (!CHECK_BIT(props, *bit)) { goto next; }
      }
      for (uint *bit = d->evbits; bit && *bit; bit++) {
        if (!CHECK_BIT(evbits, *bit)) { goto next; }
      }
      for (uint *bit = d->keybits; bit && *bit; bit++) {
        if (!CHECK_BIT(keybits, *bit)) { goto next; }
      }
      for (struct uinput_abs_setup *abs = d->abs; abs && not_empty_abs(abs); abs++) {
        if (!CHECK_BIT(absbits, abs->code)) { goto next; }
      }
      struct fd_list *fdl = malloc(sizeof(struct fd_list));
      fdl->fd = fd;
      fdl->next = d->fds;
      d->fds = fdl;
   next: ;
    }
  }
  udev_enumerate_unref(e);

  if (create_if_missing) {
    for (struct input_device *d = devices; not_empty_dev(d); ++d) {
      if (!d->fds) {
        d->fds = malloc(sizeof(struct fd_list));
        d->fds->fd = create_device(d);
        d->fds->next = NULL;
      }
    }
  }
}

struct rM_input_devices find_rm_input_devices(int create_if_missing) {
  struct input_device devices[] = { digitizer, touch, kbd, 0 };
  find_devices(devices, create_if_missing);
  struct rM_input_devices_priv *priv = malloc(sizeof(struct rM_input_devices_priv));
  *priv = (struct rM_input_devices_priv){
    .extra_wacom_fds = devices[0].fds ? devices[0].fds->next : NULL,
    .extra_touch_fds = devices[1].fds ? devices[1].fds->next : NULL,
    .extra_key_fds = devices[2].fds ? devices[2].fds->next : NULL,
    .hwe = NULL,
    .hte = NULL,
    .hke = NULL,
    .input_thread_mutex = PTHREAD_MUTEX_INITIALIZER,
    .input_thread_running = 0,
    .wd = {
      .mutex = PTHREAD_MUTEX_INITIALIZER
    },
    .td = {
      .mutex = PTHREAD_MUTEX_INITIALIZER,
    },
    .kd = {
      .mutex = PTHREAD_MUTEX_INITIALIZER,
    },
  };
  struct rM_input_devices ret = {
    .digitizer = devices[0].fds ? devices[0].fds->fd : -1,
    .touch = devices[1].fds ? devices[1].fds->fd : -1,
    .kbd = devices[2].fds ? devices[2].fds->fd : -1,
    .priv = priv,
  };
  return ret;
}

enum device_type {
  DEV_WACOM,
  DEV_TOUCH,
  DEV_KEY,
};

static void wacom_coord_evd_to_disp(int *x, int *y) {
  int yy = 1874-((1874*(*x))/DIGITIZER_MAX_X);
  int xx = 1404*(*y)/DIGITIZER_MAX_Y;
  *x = xx; *y = yy;
}
static void wacom_coord_disp_to_evd(int *x, int *y) {
  int xx = DIGITIZER_MAX_X*(1874-(*y))/1874;
  int yy = DIGITIZER_MAX_Y*(*x)/1404;
  *x = xx; *y = yy;
}
/* On rm1, both axes are inverted; on rm2, only the y-axis */
#if REMARKABLE_VERSION < 2
static void touch_coord_evd_to_disp(int *x, int *y) {
  int xx = 1404*(TOUCH_MAX_X-*x)/TOUCH_MAX_X;
  int yy = 1874*(TOUCH_MAX_Y-*y)/TOUCH_MAX_Y;
  *x = xx; *y = yy;
}
static void touch_coord_disp_to_evd(int *x, int *y) {
  int xx = (1404-*x)*TOUCH_MAX_X/1404;
  int yy = (1872-*y)*TOUCH_MAX_Y/1872;
  *x = xx; *y = yy;
}
#else
static void touch_coord_evd_to_disp(int *x, int *y) {
  int xx = 1404*(*x)/TOUCH_MAX_X;
  int yy = 1874*(TOUCH_MAX_Y-*y)/TOUCH_MAX_Y;
  *x = xx; *y = yy;
}
static void touch_coord_disp_to_evd(int *x, int *y) {
  int xx = (*x)*TOUCH_MAX_X/1404;
  int yy = (1872-*y)*TOUCH_MAX_Y/1872;
  *x = xx; *y = yy;
}
#endif

struct edata {
  enum device_type dt;
  int fd;
};
static struct edata *mk_edata(enum device_type dt, int fd) {
  struct edata *ret = malloc(sizeof(struct edata));
  ret->dt = dt;
  ret->fd = fd;
  return ret;
}
static void handle_wacom_syn_dropped(struct rM_input_devices *ds, int fd) {
  char keybits[SIZE(KEY)] = {0};
  ioctl(fd, EVIOCGKEY(SIZE(KEY)), &keybits);
  ds->priv->wd.pen_down = CHECK_BIT(keybits, BTN_TOOL_PEN);
  ds->priv->wd.touch_down = CHECK_BIT(keybits, BTN_TOUCH);
  struct input_absinfo abs = {0};
  ioctl(fd, EVIOCGABS(ABS_X), &abs);
  ds->priv->wd.abs_x = abs.value;
  ioctl(fd, EVIOCGABS(ABS_Y), &abs);
  ds->priv->wd.abs_y = abs.value;
  ioctl(fd, EVIOCGABS(ABS_PRESSURE), &abs);
  ds->priv->wd.abs_pressure = abs.value;
  ds->priv->wd.drop_until_syn = 1;
}
struct input_mt_request_layout {
  __u32 code;
  __s32 values[N_SLOTS];
};
static void update_trkid(struct touch_data *td, int kern_trkid) {
  /* For now, we don't do anything to try to map existing contacts,
   * and just hope that KERN_TRKID_OFFSET is big enough. */
  if (kern_trkid >= 0 &&
      /* TODO: fix to actually track what we're using instead of just
       * bumping if we see something <1/2 the offset */
      kern_trkid < (td->next_trkid - KERN_TRKID_OFFSET/2)&TRKID_MAX &&
      (kern_trkid + KERN_TRKID_OFFSET)&TRKID_MAX > td->next_trkid) {
    td->next_trkid = (kern_trkid + KERN_TRKID_OFFSET) & TRKID_MAX;
  }
}
static void handle_touch_syn_dropped(struct rM_input_devices *ds, int fd) {
  struct input_mt_request_layout imrl_id, imrl_x, imrl_y;
  imrl_id.code = ABS_MT_TRACKING_ID;
  ioctl(fd, EVIOCGMTSLOTS(sizeof(imrl_id)), &imrl_id);
  imrl_x.code = ABS_MT_POSITION_X;
  ioctl(fd, EVIOCGMTSLOTS(sizeof(imrl_x)), &imrl_x);
  imrl_y.code = ABS_MT_POSITION_Y;
  ioctl(fd, EVIOCGMTSLOTS(sizeof(imrl_y)), &imrl_y);
  for (int i = 0; i < N_SLOTS; ++i) {
    ds->priv->td.slots[i] = imrl_id.values[i];
    update_trkid(&ds->priv->td, imrl_id.values[i]);
    ds->priv->td.abs_x[i] = imrl_x.values[i];
    ds->priv->td.abs_y[i] = imrl_y.values[i];
    if (ds->priv->hte && imrl_id.values[i] >= 0) {
      int x = imrl_x.values[i], y = imrl_y.values[i];
      if (ds->priv->td.coord_kind & RM_COORD_DISPLAY) {
        touch_coord_evd_to_disp(&x, &y);
      }
      ds->priv->hte(ds->priv->td.userdata, imrl_id.values[i], x, y);
    }
  }
  struct input_absinfo abs;
  ioctl(fd, EVIOCGABS(ABS_MT_SLOT), &abs);
  ds->priv->td.current_slot = abs.value;
  ds->priv->td.drop_until_syn = 1;
}
static void handle_wacom_event(struct rM_input_devices *ds, int fd) {
  struct input_event ev;
  struct wacom_data *wd = &ds->priv->wd;
  pthread_mutex_lock(&wd->mutex);
  while (read(fd, &ev, sizeof(struct input_event)) == sizeof(struct input_event)) {
    if (ev.type == EV_SYN) {
      if (ev.code == SYN_DROPPED) { handle_wacom_syn_dropped(ds, fd); }
      if (ev.code == SYN_REPORT) {
        if (wd->drop_until_syn) { wd->drop_until_syn = 0; continue; }
        if (ds->priv->hwe) {
          int x = wd->abs_x; int y = wd->abs_y;
          if (wd->coord_kind & RM_COORD_DISPLAY) {
            wacom_coord_evd_to_disp(&x, &y);
          }
          ds->priv->hwe(wd->userdata, wd->pen_down, wd->touch_down,
                        x, y, wd->abs_pressure);
        }
      }
    }
    if (wd->drop_until_syn) { continue; }
    if (ev.type == EV_KEY) {
      if (ev.code == BTN_TOOL_PEN) { wd->pen_down = ev.value; }
      if (ev.code == BTN_TOUCH) { wd->touch_down = ev.value; }
    }
    if (ev.type == EV_ABS) {
      if (ev.code == ABS_X) { wd->abs_x = ev.value; }
      if (ev.code == ABS_Y) { wd->abs_y = ev.value; }
      if (ev.code == ABS_PRESSURE) { wd->abs_pressure = ev.value; }
    }
  }
  pthread_mutex_unlock(&wd->mutex);
}
static void handle_touch_event(struct rM_input_devices *ds, int fd) {
  struct input_event ev;
  struct touch_data *td = &ds->priv->td;
  pthread_mutex_lock(&td->mutex);
  while (read(fd, &ev, sizeof(struct input_event)) == sizeof(struct input_event)) {
    if (ev.type == EV_SYN) {
      if (ev.code == SYN_DROPPED) { handle_touch_syn_dropped(ds, fd); }
      if (ev.code == SYN_REPORT) {
        if (td->drop_until_syn) { td->drop_until_syn = 0; continue; }
        if (ds->priv->hte) {
          for (int i = 0; i < N_SLOTS; ++i) {
            if (td->slots[i] >= 0) {
              int x = td->abs_x[i]; int y = td->abs_y[i];
              if (td->coord_kind & RM_COORD_DISPLAY) {
                touch_coord_evd_to_disp(&x, &y);
              }
              ds->priv->hte(td->userdata, td->slots[i], x, y);
            }
          }
        }
      }
    }
    if (td->drop_until_syn) { continue; }
    if (ev.type == EV_ABS) {
      if (ev.code == ABS_MT_SLOT) {
        td->current_slot = ev.value;
      }
      if (ev.code == ABS_MT_TRACKING_ID) {
        td->slots[td->current_slot] = ev.value;
        update_trkid(td, ev.value);
      }
      if (ev.code == ABS_MT_POSITION_X) {
        td->abs_x[td->current_slot] = ev.value;
      }
      if (ev.code == ABS_MT_POSITION_Y) {
        td->abs_y[td->current_slot] = ev.value;
      }
    }
  }
  pthread_mutex_unlock(&td->mutex);
}
static void handle_key_event(struct rM_input_devices *ds, int fd) {
  struct input_event ev;
  pthread_mutex_lock(&ds->priv->kd.mutex);
  while (read(fd, &ev, sizeof(struct input_event)) == sizeof(struct input_event)) {
    /* TODO: we should wait for SYN_REPORT (and handle SYN_DROPPED) */
    if (ev.type == EV_KEY) {
      if (ds->priv->hke) {
        ds->priv->hke(ds->priv->kd.userdata, ev.code, ev.value);
      }
    }
  }
  pthread_mutex_unlock(&ds->priv->kd.mutex);
}
static int add_epoll_event(int epfd, int fd, enum device_type t) {
  struct epoll_event ev;
  ev.events = EPOLLIN;
  int flags;
  if ((flags = fcntl(fd, F_GETFL, 0)) < 0) { flags = 0; }
  if (fcntl(fd, F_SETFL, flags|O_NONBLOCK) < 0) { return -1; }
  ev.data.ptr = mk_edata(t, fd);
  if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == -1) { return -1; }
  return 0;
}
static void *run_input_thread(void *ds_) {
  struct rM_input_devices *ds = (struct rM_input_devices *)ds_;
  pthread_mutex_lock(&ds->priv->input_thread_mutex);
  if (ds->priv->input_thread_running) { goto err; }

  int epfd = epoll_create(3);

  if (add_epoll_event(epfd, ds->digitizer, DEV_WACOM) < 0) { goto err; }
  for (struct fd_list *f = ds->priv->extra_wacom_fds; f; f = f->next) {
    if (add_epoll_event(epfd, f->fd, DEV_WACOM) < 0) { goto err; }
  }
  if (add_epoll_event(epfd, ds->touch, DEV_TOUCH) < 0) { goto err; }
  for (struct fd_list *f = ds->priv->extra_touch_fds; f; f = f->next) {
    if (add_epoll_event(epfd, f->fd, DEV_TOUCH) < 0) { goto err; }
  }
  if (add_epoll_event(epfd, ds->kbd, DEV_KEY) < 0) { goto err; }
  for (struct fd_list *f = ds->priv->extra_key_fds; f; f = f->next) {
    if (add_epoll_event(epfd, f->fd, DEV_KEY) < 0) { goto err; }
  }

  ds->priv->input_thread_running = 1;
  pthread_mutex_unlock(&ds->priv->input_thread_mutex);

  handle_wacom_syn_dropped(ds, ds->digitizer);
  ds->priv->wd.drop_until_syn = 0;
  handle_touch_syn_dropped(ds, ds->touch);
  ds->priv->td.drop_until_syn = 0;

  while (1) {
#define MAX_EVENTS 5
    struct epoll_event events[MAX_EVENTS];
    int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
    if (nfds == -1) {
      pthread_mutex_lock(&ds->priv->input_thread_mutex);
      ds->priv->input_thread_running = 0;
      pthread_mutex_unlock(&ds->priv->input_thread_mutex);
    }
    for (int n = 0; n < nfds; ++n) {
      struct edata *ed = (struct edata *)events[n].data.ptr;
      switch (ed->dt) {
        case DEV_WACOM:
          handle_wacom_event(ds, ed->fd);
          break;
        case DEV_TOUCH:
          handle_touch_event(ds, ed->fd);
          break;
        case DEV_KEY:
          handle_key_event(ds, ed->fd);
          break;
      }
    }
  }

  return NULL;

err:
  pthread_mutex_unlock(&ds->priv->input_thread_mutex);
  return NULL;
}

int enable_input_event_listening(struct rM_input_devices *ds) {
  pthread_mutex_lock(&ds->priv->input_thread_mutex);
  if (ds->priv->input_thread_running) { return 0; }
  pthread_mutex_unlock(&ds->priv->input_thread_mutex);
  for (int i = 0; i < 32; ++i) {
    ds->priv->td.slots[i] = -1;
  }
  ds->priv->td.current_slot = -1;
  ds->priv->td.next_trkid = 1; /* will be updated next time we see an event from the kernel */
  int ret = pthread_create(&ds->priv->input_thread, NULL, run_input_thread, ds);
  if (!ret) { return ret; }
}

int submit_wacom_event(struct rM_input_devices *ds,
                       int pen_down, int touch_down,
                       struct rM_coord coord, int abs_pressure,
                       uint which) {
  int x = coord.x; int y = coord.y;
  if (coord.coord_kind & RM_COORD_DISPLAY) {
    wacom_coord_disp_to_evd(&x, &y);
  }
  struct input_event ies[6] = {0};
  int next = 0;
  if (which & WHICH_WACOM_PEN) {
    ies[next].type = EV_KEY; ies[next].code = BTN_TOOL_PEN;
    ies[next].value = pen_down;
    next++;
  }
  if (which & WHICH_WACOM_TOUCH) {
    ies[next].type = EV_KEY; ies[next].code = BTN_TOUCH;
    ies[next].value = touch_down;
    next++;
  }
  if (which & WHICH_WACOM_X) {
    ies[next].type = EV_ABS; ies[next].code = ABS_X;
    ies[next].value = x;
    next++;
  }
  if (which & WHICH_WACOM_Y) {
    ies[next].type = EV_ABS; ies[next].code = ABS_Y;
    ies[next].value = y;
    next++;
  }
  if (which & WHICH_WACOM_PRESSURE) {
    ies[next].type = EV_ABS; ies[next].code = ABS_PRESSURE;
    ies[next].value = abs_pressure;
    next++;
  }
  ies[next].type = EV_SYN;
  ies[next].code = SYN_REPORT;
  ies[next].value = 0;
  next++;
  return write(ds->digitizer, ies, sizeof(struct input_event)*next);
}
int on_wacom_event(struct rM_input_devices *ds, uint coord_kind,
                   handle_wacom_event_t handle, void *data) {
  pthread_mutex_lock(&ds->priv->wd.mutex);
  ds->priv->hwe = handle;
  ds->priv->wd.userdata = data;
  ds->priv->wd.coord_kind = coord_kind;
  pthread_mutex_unlock(&ds->priv->wd.mutex);
}

int touch_begin_contact(struct rM_input_devices *ds) {
  struct touch_data *td = &ds->priv->td;
  pthread_mutex_lock(&td->mutex);
  int id = td->next_trkid;
  td->next_trkid = (id+1)&TRKID_MAX;

  int slot = -1;
  for (int i = N_SLOTS; i >= 0; --i) {
    if (td->slots[i] < 0) { slot = i; break; }
  }
  if (slot < 0) { pthread_mutex_unlock(&td->mutex); return -1; /* out of slots */ }
  td->slots[slot] = id;

  pthread_mutex_unlock(&td->mutex);
  return id;
}
int submit_touch_contact(struct rM_input_devices *ds, int c,
                         struct rM_coord coord, int which) {
  if (c < 0) { return -1; }
  struct touch_data *td = &ds->priv->td;
  pthread_mutex_lock(&td->mutex);
  int x = coord.x; int y = coord.y;
  if (coord.coord_kind & RM_COORD_DISPLAY) {
    touch_coord_disp_to_evd(&x, &y);
  }
  int slot = -1;
  for (int i = N_SLOTS; i >= 0; --i) {
    if (td->slots[i] == c) { slot = i; break; }
  }
  if (slot < 0) { pthread_mutex_unlock(&td->mutex); return -1; }
  /* Set slot, set tracking id, set x/y, syn report, set tracking id, set slot */
  struct input_event ies[6] = {0};
  int next = 0;
  ies[next].type = EV_ABS; ies[next].code = ABS_MT_SLOT; ies[next].value = slot;
  next++;
  ies[next].type = EV_ABS; ies[next].code = ABS_MT_TRACKING_ID;
  ies[next].value = c; next++;
  if (which & WHICH_TOUCH_X) {
    ies[next].type = EV_ABS; ies[next].code = ABS_MT_POSITION_X;
    ies[next].value = x; next++;
  }
  if (which & WHICH_TOUCH_Y) {
    ies[next].type = EV_ABS; ies[next].code = ABS_MT_POSITION_Y;
    ies[next].value = y; next++;
  }
  ies[next].type = EV_ABS; ies[next].code = ABS_MT_SLOT;
  ies[next].value = td->current_slot; next++;
  ies[next].type = EV_SYN; ies[next].code = SYN_REPORT; ies[next].value = 0;
  next++;
  pthread_mutex_unlock(&td->mutex);
  return write(ds->touch, ies, sizeof(struct input_event)*next);
}
int touch_end_contact(struct rM_input_devices *ds, int c) {
struct touch_data *td = &ds->priv->td;
  pthread_mutex_lock(&td->mutex);

  int slot = -1;
  for (int i = N_SLOTS; i >= 0; --i) {
    if (td->slots[i] == c) { slot = i; break; }
  }
  if (slot < 0) { pthread_mutex_unlock(&td->mutex); return -1; }

  struct input_event ies[4] = {
    { .type = EV_ABS, .code = ABS_MT_SLOT, .value = slot },
    { .type = EV_ABS, .code = ABS_MT_TRACKING_ID, .value = -1 },
    { .type = EV_ABS, .code = ABS_MT_SLOT, .value = td->current_slot },
    { .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
  };
  td->slots[slot] = -1;

  pthread_mutex_unlock(&td->mutex);
  return write(ds->touch, ies, sizeof(ies));

}
int on_touch_event(struct rM_input_devices *ds, uint coord_kind,
                   handle_touch_event_t handle, void *data) {
  pthread_mutex_lock(&ds->priv->td.mutex);
  ds->priv->hte = handle;
  ds->priv->td.userdata = data;
  ds->priv->td.coord_kind = coord_kind;
  pthread_mutex_unlock(&ds->priv->td.mutex);
}

int submit_key_event(struct rM_input_devices *ds, int key, int down) {
  struct input_event ies[2] = {
    { .type = EV_KEY, .code = key, .value = down },
    { .type = EV_SYN, .code = SYN_REPORT, .value = 0 },
  };
  return write(ds->kbd, ies, sizeof(ies));
}
int on_key_event(struct rM_input_devices *ds,
                 handle_key_event_t handle, void *data) {
  pthread_mutex_lock(&ds->priv->kd.mutex);
  ds->priv->hke = handle;
  ds->priv->kd.userdata = data;
  pthread_mutex_unlock(&ds->priv->kd.mutex);
}
