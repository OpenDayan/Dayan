
#include "utils/logger.h"
#define FUSE_USE_VERSION 33

#include <fcntl.h>
#include <fuse3/fuse_lowlevel.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <span>
#include <string>
#include <vector>

#include "devices/axi4master.h"
#include "devices/ddr4.h"

namespace FuseDriver {

auto logger = spdlog::stderr_color_mt("FUSE");

struct Node {
  fuse_ino_t ino;
  std::string name;
  struct stat stbuf;
};

const size_t max_size = 6 * (1024 * 1024 * 1024ULL);
const uint64_t mem_addr_offset = 0x80000000;

std::map<fuse_ino_t, struct Node> nodes = {
    {FUSE_ROOT_ID,
     {
         .ino = FUSE_ROOT_ID,
         .name = "/",
         .stbuf =
             {
                 .st_nlink = 2,
                 .st_mode = S_IFDIR | 0755,
                 .st_uid = getuid(),
                 .st_gid = getgid(),
             },
     }},
    {2,
     {
         .ino = 2,
         .name = "xdma0_h2c_0",
         .stbuf =
             {
                 .st_nlink = 1,
                 .st_mode = S_IFREG | 0666,
                 .st_uid = getuid(),
                 .st_gid = getgid(),
                 .st_size = max_size,
             },
     }},
    {3,
     {
         .ino = 3,
         .name = "xdma0_c2h_0",
         .stbuf =
             {
                 .st_nlink = 1,
                 .st_mode = S_IFREG | 0666,
                 .st_uid = getuid(),
                 .st_gid = getgid(),
                 .st_size = max_size,
             },
     }},
};

static void init(void *userdata, struct fuse_conn_info *conn) {
  (void)userdata;
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  for (auto &pair : nodes) {
    Node &node = pair.second;
    node.stbuf.st_atim = ts;
    node.stbuf.st_mtim = ts;
    node.stbuf.st_ctim = ts;
  }
}

static int stat(fuse_ino_t ino, struct stat *stbuf) {
  stbuf->st_ino = ino;
  auto it = nodes.find(ino);
  if (it != nodes.end()) {
    *stbuf = it->second.stbuf;
    return 0;
  }
  return -1;
}

static void getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
  (void)fi;
  struct stat stbuf;
  if (stat(ino, &stbuf) == -1)
    fuse_reply_err(req, ENOENT);
  else
    fuse_reply_attr(req, &stbuf, 1.0);
}

static void lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
  if (parent != FUSE_ROOT_ID) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  for (const auto &pair : nodes) {
    const Node &node = pair.second;
    if (node.name == name) {
      struct fuse_entry_param e;
      memset(&e, 0, sizeof(e));
      e.ino = node.ino;
      e.attr = node.stbuf;
      e.attr_timeout = 1.0;
      e.entry_timeout = 1.0;
      fuse_reply_entry(req, &e);
      return;
    }
  }

  fuse_reply_err(req, ENOENT);
}

static void open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
  (void)fi;
  if (nodes.find(ino) == nodes.end()) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  fi->direct_io = 1;
  fi->keep_cache = 0;
  fuse_reply_open(req, fi);
}

static void read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                 struct fuse_file_info *fi) {
  (void)fi;

  if (off >= (off_t)max_size) {
    printf("FUSE try to read illegal address 0x%08lx\n", off);
    fuse_reply_buf(req, NULL, 0);
    return;
  }
  if (off + size > max_size) {
    size = max_size - off;
  }

  if (off >= mem_addr_offset) {
    auto data_ptr = DDR4::fuse_read(off, size);
    fuse_reply_buf(req, (const char *)data_ptr.data(), size);
    return;
  }

  auto data_ptr = AXI4Master.read(off, size);
  if (!data_ptr) {
    fuse_reply_err(req, EIO);
    return;
  }
  fuse_reply_buf(req, (const char *)data_ptr->data(), size);
}

static void write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size,
                  off_t off, struct fuse_file_info *fi) {
  (void)fi;

  if (off >= (off_t)max_size) {
    printf("FUSE try to write illegal address 0x%08lx\n", off);
    fuse_reply_write(req, 0);
    return;
  }
  if (off + size > max_size) {
    size = max_size - off;
  }

  if (off >= mem_addr_offset) {
    DDR4::fuse_write(off, std::span((const uint8_t *)buf, size));
    fuse_reply_write(req, size);
    return;
  }

  auto ret =
      AXI4Master.write(off, std::span((const uint8_t *)buf, size));
  if (ret < 0) {
    fuse_reply_err(req, EIO);
    return;
  }
  fuse_reply_write(req, size);
}

struct dirbuf {
  char *p;
  size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
                       fuse_ino_t ino) {
  struct stat stbuf;
  size_t oldsize = b->size;
  b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
  b->p = (char *)realloc(b->p, b->size);
  memset(&stbuf, 0, sizeof(stbuf));
  stbuf.st_ino = ino;
  fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
                    b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
                             off_t off, size_t maxsize) {
  if (off < bufsize)
    return fuse_reply_buf(req, buf + off, min(bufsize - off, maxsize));
  else
    return fuse_reply_buf(req, NULL, 0);
}

static void readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
                    struct fuse_file_info *fi) {
  (void)fi;
  if (ino != FUSE_ROOT_ID) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  struct dirbuf b;

  memset(&b, 0, sizeof(b));
  dirbuf_add(req, &b, ".", 1);
  dirbuf_add(req, &b, "..", 1);
  for (const auto &pair : nodes) {
    const Node &node = pair.second;
    if (node.ino != FUSE_ROOT_ID) {
      const char *name = node.name.c_str();
      dirbuf_add(req, &b, name, node.ino);
    }
  }
  reply_buf_limited(req, b.p, b.size, off, size);
  free(b.p);
}

static const struct fuse_lowlevel_ops oper = {
    .init = init,
    .lookup = lookup,
    .getattr = getattr,
    .open = open,
    .read = read,
    .write = write,
    .readdir = readdir,
};

struct fuse_session *se;

void session_stop() {
  if (se) {
    fuse_session_exit(se);
    // fuse_session_unmount(se);
  }
}

bool session_exited() {
  if (se) {
    return fuse_session_exited(se);
  }
  return true;
}

int main(int argc, char *argv[]) {
  logger->set_level(spdlog::level::info);
  logger->set_formatter(std::move(make_sim_formatter()));

  struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
  struct fuse_cmdline_opts opts;
  struct fuse_loop_config config;
  int ret = -1;

  if (fuse_parse_cmdline(&args, &opts) != 0) return 1;
  if (opts.show_help) {
    printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
    fuse_cmdline_help();
    fuse_lowlevel_help();
    ret = 0;
    goto err_out1;
  } else if (opts.show_version) {
    printf("FUSE library version %s\n", fuse_pkgversion());
    fuse_lowlevel_version();
    ret = 0;
    goto err_out1;
  }

  if (opts.mountpoint == NULL) {
    printf("usage: %s [options] <mountpoint>\n", argv[0]);
    printf("       %s --help\n", argv[0]);
    ret = 1;
    goto err_out1;
  }

  se = fuse_session_new(&args, &oper, sizeof(oper), NULL);
  if (se == NULL) goto err_out1;

  if (fuse_set_signal_handlers(se) != 0) goto err_out2;

  if (fuse_session_mount(se, opts.mountpoint) != 0) goto err_out3;

  logger->info("FUSE filesystem mounted at {}", opts.mountpoint);

  // fuse_daemonize(opts.foreground);

  /* Block until ctrl+c or fusermount -u */
  if (opts.singlethread)
    ret = fuse_session_loop(se);
  else {
    config.clone_fd = opts.clone_fd;
    config.max_idle_threads = opts.max_idle_threads;
    ret = fuse_session_loop_mt(se, &config);
  }

  fuse_session_unmount(se);
err_out3:
  // fuse_remove_signal_handlers(se);
err_out2:
  fuse_session_destroy(se);
err_out1:
  free(opts.mountpoint);
  fuse_opt_free_args(&args);

  return ret ? 1 : 0;
}

}  // namespace FuseDriver
