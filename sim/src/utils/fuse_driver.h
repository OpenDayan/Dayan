#ifndef FUSE_DRIVER_H
#define FUSE_DRIVER_H

namespace FuseDriver {
int main(int argc, char *argv[]);
bool session_exited();
void session_stop();

}  // namespace FuseDriver

#endif  // FUSE_DRIVER_H
