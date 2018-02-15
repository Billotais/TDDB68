#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/init.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"
#include "lib/kernel/stdio.h"
#include "threads/vaddr.h"


static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
bool valid_pointer(void* ptr)
{
    return (ptr < PHYS_BASE && pagedir_get_page(thread_current()->pagedir, ptr) != NULL);
}
void exit(int exit_value)
{
    struct thread* calling_thread = thread_current();
    // Make the exit value availible for the parents
    calling_thread->parent->exit_status = exit_value;
    // Wake up any parent that might be waiting
    sema_up(&calling_thread->parent->sema);
    //calling_thread->parent->has_already_wait = true;

    // For each file
    for (int i = 0; i < MAX_FILES + NB_RESERVED_FILES; ++i)
    {
        // If open, close it
        if (calling_thread->files[i] != NULL)
        {
            file_close(calling_thread->files[i]);
            calling_thread->files[i] = NULL;
        }
    }
    // exit the thread

    printf("%s: exit(%d)\n", calling_thread->name, exit_value);
    thread_exit ();
}
void valid_string(const char* ptr)
{
    char* prov = ptr;
    do {
        if (!valid_pointer(prov)) exit(-1);
        prov +=1;
    } while(*prov != '\0');

}
void valid_buffer(void* ptr, unsigned buf_size)
{
    void* prov = ptr;
    for (unsigned i = 0; i < buf_size; ++i)
    {
        if (!valid_pointer(prov)) exit(-1);
    }


}
void* incr_and_check(void* ptr)
{
    ptr+=4;
    if (!valid_pointer(ptr)) exit(-1);
    return ptr;
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  // We get the stack pointer, casting it to int*
  int* user_stack = (int*)f->esp;
  if (!valid_pointer(user_stack)) exit(-1);


  // Now we execute the correct code depending on the tzpe of szstem call.

  if (*user_stack == SYS_HALT)
  {
      power_off();
  }
  else if (*user_stack == SYS_CREATE)
  {
      // Try to create a file, and return whatever the filesysem's function returns.
      user_stack = incr_and_check(user_stack);
      const char* file_name = (const char*)*user_stack;
      valid_string(file_name);
      user_stack = incr_and_check(user_stack);
      unsigned initial_size = (unsigned)*user_stack;
      f->eax = filesys_create(file_name, initial_size);
  }
  else if (*user_stack == SYS_OPEN)
  {
      // We trz to open the file
      user_stack = incr_and_check(user_stack);
      const char* file_name = (const char*)*user_stack;
      valid_string(file_name);
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
      user_stack = incr_and_check(user_stack);
      int fd = *user_stack;
      if (0 > fd || fd >= MAX_FILES + NB_RESERVED_FILES) return;
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
      user_stack = incr_and_check(user_stack);
      int fd = *user_stack;
      // If we want to read from the console
      if (fd == STDIN_FILENO)
      {
          user_stack = incr_and_check(user_stack);
          char* buffer = (char *)*user_stack;
          user_stack = incr_and_check(user_stack);
          size_t to_read = *user_stack;
          valid_buffer(buffer, to_read);

          // Check that the user can access this memory
          if ((void*)buffer >= PHYS_BASE)
          {
              f->eax = -1;
              return;
          }
          // Read from the console to_read times
          for (size_t i = 0; i < to_read; ++i)
          {
              buffer[i] = input_getc();
          }
          f->eax = to_read;
          return;
      }

      // If we want to read from the file
      else if(fd != STDOUT_FILENO)
      {
          if (0 > fd || fd >= MAX_FILES + NB_RESERVED_FILES)
          {
              f->eax = -1;
              return;
          }
          // Get the file associated to the fd, exit if doesn't exist
          struct thread* calling_thread = thread_current();
          struct file* to_read = calling_thread->files[fd];
          if (to_read == NULL)
          {
              f->eax = -1;
              return;
          }
          // Read from the
          user_stack = incr_and_check(user_stack);
          void* buffer = (void *)(*user_stack);

          user_stack = incr_and_check(user_stack);
          unsigned size_to_read = (unsigned)*user_stack;
          valid_buffer(buffer, size_to_read);
          int read = file_read(to_read, buffer, size_to_read);

          f->eax = read;
          return;
      }
      f->eax = -1;
  }
  else if (*user_stack == SYS_WRITE)
  {
      user_stack = incr_and_check(user_stack);
      int fd = *user_stack;

      // If we want to wrtie to the console
      if (fd == STDOUT_FILENO)
      {
          user_stack = incr_and_check(user_stack);
          const char* to_write = (const char*)(*user_stack);
          // Get the size to write, limit it if too big
          user_stack = incr_and_check(user_stack);
          size_t size_to_write = (size_t)*user_stack;
          valid_buffer((void*)to_write, size_to_write);
          size_to_write = size_to_write > MAX_BYTES_CONSOLE ? MAX_BYTES_CONSOLE : size_to_write;
          // Write to the console

          putbuf(to_write, size_to_write);

          f->eax = size_to_write;
          return;
      }
      // If we want to write to the file
      else if (fd != STDIN_FILENO)
      {
          if (0 > fd || fd >= MAX_FILES + NB_RESERVED_FILES)
          {
              f->eax = -1;
              return;
          }
          // Get the associated file, exit if no existing
          struct thread* calling_thread = thread_current();
          struct file* to_write = calling_thread->files[fd];
          if (to_write == NULL)
          {
              f->eax = -1;
              return;
          }
          // Write to the file
          user_stack = incr_and_check(user_stack);
          void* buffer = (void*)*user_stack;
          if (buffer >= PHYS_BASE)
          {
              f->eax = -1;
              return;
          }
          user_stack = incr_and_check(user_stack);
          unsigned size_to_write = (unsigned)*user_stack;
          valid_buffer(buffer, size_to_write);
          int written = file_write(to_write, buffer, size_to_write);

          f->eax = written;
          return;
      }
      f->eax = -1;
  }
  else if (*user_stack == SYS_EXEC)
  {
      user_stack = incr_and_check(user_stack);
      const char* cmd_line = (const char*)*user_stack;
      valid_string(cmd_line);

      tid_t tid = process_execute(cmd_line);
      f->eax = tid;

  }
  else if (*user_stack == SYS_WAIT)
  {
      user_stack = incr_and_check(user_stack);
      tid_t id = (tid_t)*user_stack;
      int exit_value = process_wait(id);
      f->eax = exit_value;


  }
  else if (*user_stack == SYS_EXIT)
  {
      // Get the calling thread
      user_stack = incr_and_check(user_stack);
      int exit_value = *user_stack;
      exit(exit_value);

  }
}
