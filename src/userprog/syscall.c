#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "lib/kernel/stdio.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  // We get the stack pointer, casting it to int*
  int* user_stack = (int*)f->esp;

  // Now we execute the correct code depending on the tzpe of szstem call.

  if (*user_stack == SYS_HALT)
  {
      power_off();
  }
  else if (*user_stack == SYS_CREATE)
  {
      // Try to create a file, and return whatever the filesysem's function returns.
      const char* file_name = (const char*)user_stack[1];
      unsigned initial_size = (unsigned)user_stack[2];
      f->eax = filesys_create(file_name, initial_size);
  }
  else if (*user_stack == SYS_OPEN)
  {
      // We trz to open the file
      const char* file_name = (const char*)user_stack[1];
      struct file* opened = filesys_open(file_name);

      // If we can't, error
      if (opened == NULL)
      {
          f->eax = -1;
          return;
      }

      // We find thread that is calling us
      struct thread* calling_thread = thread_current();

      // We go through all potential file slots (except 0 and 1)
      for (size_t i = NB_RESERVED_FILES; i < MAX_FILES + NB_RESERVED_FILES; ++i)
      {
          // If a slot is available, we write in it
          if (calling_thread->files[i] == NULL)
          {
              calling_thread->files[i] = opened;
              f->eax = i;
              return;
          }
      }
      // If nothing found, error
      f->eax = -1;

  }
  else if (*user_stack == SYS_CLOSE)
  {
      // Get the file associated to the given fd
      int fd = user_stack[1];
      struct thread* calling_thread = thread_current();
      struct file* to_close = calling_thread->files[fd];

      // If no file is found, exit
      if (to_close == NULL) return;

      // Other close it and free the space in the array
      file_close(to_close);
      calling_thread->files[fd] = NULL;
  }
  else if (*user_stack == SYS_READ)
  {

      int fd = user_stack[1];
      // If we want to read from the console
      if (fd == STDIN_FILENO)
      {
          size_t to_read = user_stack[3];

          // Read from the console to_read times
          for (size_t i = 0; i < to_read; ++i)
          {
              char* buffer = (char *)user_stack[2];
              buffer[i] = input_getc();
          }
          f->eax = to_read;
          return;
      }

      // If we want to read from the file
      else if(fd != STDOUT_FILENO)
      {
          // Get the file associated to the fd, exit if doesn't exist
          struct thread* calling_thread = thread_current();
          struct file* to_read = calling_thread->files[fd];
          if (to_read == NULL)
          {
              f->eax = -1;
              return;
          }
          // Read from the file
          void* buffer = (void *)(user_stack[2]);
          unsigned size_to_read = (unsigned)user_stack[3];
          int read = file_read(to_read, buffer, size_to_read);

          f->eax = read;
          return;
      }
      f->eax = -1;
  }
  else if (*user_stack == SYS_WRITE)
  {
      int fd = user_stack[1];

      // If we want to wrtie to the console
      if (fd == STDOUT_FILENO)
      {
          // Get the size to write, limit it if too big
          size_t size_to_write = (size_t)user_stack[3];
          size_to_write = size_to_write > MAX_BYTES_CONSOLE ? MAX_BYTES_CONSOLE : size_to_write;

          // Write to the console
          const char* to_write = (const char*)(user_stack[2]);
          putbuf(to_write, size_to_write);

          f->eax = size_to_write;
          return;
      }
      // If we want to write to the file
      else if (fd != STDIN_FILENO)
      {
          // Get the associated file, exit if no existing
          struct thread* calling_thread = thread_current();
          struct file* to_write = calling_thread->files[fd];
          if (to_write == NULL)
          {
              f->eax = -1;
              return;
          }
          // Write to the file
          void* buffer = (void*)user_stack[2];
          unsigned size_to_write = (unsigned)user_stack[3];
          int written = file_write(to_write, buffer, size_to_write);

          f->eax = written;
          return;
      }
      f->eax = -1;
  }
  else if (*user_stack == SYS_EXIT)
  {
      // Get the calling thread
      f->eax = user_stack[1];
      struct thread* calling_thread = thread_current();

      // For each file
      for (int i = 0; i < MAX_FILES + NB_RESERVED_FILES; ++i)
      {
          // If open, close it
          if (calling_thread->files[i] != NULL)
          {
              file_close(calling_thread->files[i]);
          }
      }
      // exit the thread
      thread_exit ();
  }
}
