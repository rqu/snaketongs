#ifndef SNAKETONGS_SUBPROC_H_
#define SNAKETONGS_SUBPROC_H_

#include <stddef.h>

#ifdef __cplusplus
namespace snaketongs::detail {
extern "C" {
#endif

struct snaketongs_impl;

struct snaketongs_impl *snaketongs_impl_start(int int_size);
bool snaketongs_impl_send(struct snaketongs_impl *self, const void *src, size_t size);
bool snaketongs_impl_flush(struct snaketongs_impl *self);
bool snaketongs_impl_recv(struct snaketongs_impl *self, void *dest, size_t size);
bool snaketongs_impl_quit(struct snaketongs_impl *self);

#ifdef __cplusplus
}
}
#endif

#endif
