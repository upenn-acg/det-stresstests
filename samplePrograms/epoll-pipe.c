/* build with `-std=c11 -O -pthread -D_GNU_SOURCE=1` */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>

#define BUFF_SIZE 4096
#define NTESTS 5

#ifdef DEBUG
#define debug(...) fprintf (stderr, __VA_ARGS__)
#else
#define debug(...)
#endif

static pthread_cond_t writer_ready = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cond_mutex = PTHREAD_MUTEX_INITIALIZER;

static int epoll_fd;

static void delay(unsigned long microseconds) {
  struct timespec tp, tp1;

  tp.tv_sec = microseconds / 1000000;
  tp.tv_nsec = 1000 * (microseconds % 1000000);

  while (1) {
    if (nanosleep(&tp, &tp1) == 0)
      break;
    if (errno == EINTR) {
      tp.tv_sec = tp1.tv_sec;
      tp.tv_nsec = tp1.tv_nsec;
    } else {
      fprintf(stderr, "nanosleep failed: %s\n", strerror(errno));
      assert(0);
      break;
    }
  }
}

static ssize_t write_all(int fd, const void* buff, size_t count) {
  long remains = (long)count;
  const char* p = (const char*)buff;
  while (remains > 0) {
    ssize_t n = write(fd, p, remains);
    if (n == remains) {
      break;
    }
    if (n == 0) {
      fprintf(stderr, "write(%d, %p, %lu) returned %ld, error: %s\n",
	      fd, buff, remains, n, strerror(errno));
      assert(0);
    } else if (n > 0) {
      remains -= n;
      p += n;
    } else {
      if (errno == EINTR) {
	continue;
      } else {
	fprintf(stderr, "write(%d, %p, %lu) returned %ld, error: %s\n",
		fd, buff, remains, n, strerror(errno));
	assert(0);
      }
    }
  }
  return count;
}

static void echo_nonblock(int fd) {
  char buff[1 + BUFF_SIZE];

  ssize_t n = read(fd, buff, BUFF_SIZE);
  if (n < 0) {
    fprintf(stderr, "read returned %ld, error: %s\n", n, strerror(errno));
    assert(0);
  }
  if (n > 0) {
    write_all(STDOUT_FILENO, buff, n);
  }
}

static void* reader_thread(void* param) {
  int fd = (int)(long)param;

  struct epoll_event ev = {
    .events = EPOLLIN,
    .data.fd = fd,
  };
  assert(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == 0);
  debug("[reader] wait for signal\n");
  assert(pthread_cond_wait(&writer_ready, &cond_mutex) == 0);
  debug("[reader] received signal\n");
  assert(pthread_mutex_unlock(&cond_mutex) == 0);
  struct epoll_event next;
  for (int i = 0; i < NTESTS; i++) {
    debug("[reader] %u epoll_wait\n", i);
    int n = epoll_wait(epoll_fd, &next, 1, -1);
    if (n == 0) break;
    if (next.events == EPOLLIN) {
      int fd_ready = next.data.fd;
      echo_nonblock(fd_ready);
    } else if (next.events == EPOLLERR) {
      fprintf(stderr, "got EPOLLERR, fd = %d\n", next.data.fd);
      break;
    } else {
      fprintf(stderr, "got unknown events: %d\n", next.events);
      assert(0);
      break;
    }
  }

  return NULL;
}

static void* writer_thread(void* param) {
  int fd = (int)(long)param;
  char buff[BUFF_SIZE];

  debug("[writer] wait for mutex\n");
  assert(pthread_mutex_lock(&cond_mutex) == 0);
  assert(pthread_cond_signal(&writer_ready) == 0);
  debug("[writer] reader signalled\n");
  assert(pthread_mutex_unlock(&cond_mutex) == 0);

  for (int i = 0; i < NTESTS; i++) {
    delay(100000);
    int n = snprintf(buff, BUFF_SIZE, "send message %u\n", i);
    write_all(fd, buff, n);
  }

  return NULL;
}

int main(int argc, char* argv[])
{
  int fd[2];
  pthread_t reader, writer;

  assert(pipe2(fd, O_CLOEXEC | O_NONBLOCK) == 0);

  epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  assert(epoll_fd >= 0);

  assert(pthread_mutex_lock(&cond_mutex) == 0);

  assert(pthread_create(&reader, NULL, reader_thread, (void*)(long)fd[0]) == 0);
  assert(pthread_create(&writer, NULL, writer_thread, (void*)(long)fd[1]) == 0);

  /* writer should finish first */
  pthread_join(writer, NULL);
  assert(epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd[0], NULL) == 0);
  pthread_join(reader, NULL);

  assert(pthread_mutex_destroy(&cond_mutex) == 0);

  close(epoll_fd);
  close(fd[1]);
  close(fd[0]);

  return 0;
}
