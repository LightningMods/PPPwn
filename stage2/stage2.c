/*
 * Copyright (C) 2024 Andy Nguyen
 *
 * This software may be modified and distributed under the terms
 * of the MIT license.  See the LICENSE file for details.
 */

// clang-format off
#define _KERNEL
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/sysent.h>
#include <sys/sysproto.h>
#include <sys/syscall.h>
#include <sys/pcpu.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/stat.h>
#include <machine/specialreg.h>
#include "offsets.h"

// by OSM-Made
typedef struct {
  int type;
  int reqId;
  int priority;
  int msgId;
  int targetId;
  int userId;
  int unk1;
  int unk2;
  int appId;
  int errorNum;
  int unk3;
  unsigned char useIconImageUri;
  char message[1024];
  char iconUri[1024];
  char unk[1024];
} OrbisNotificationRequest;

struct sysent *sysents = NULL;
struct thread * td = NULL;

size_t strlen(const char * s) {
  const char * t = s;
  while (* t)
    t++;
  return t - s;
}

static inline uint64_t rdmsr(u_int msr) {
  uint32_t low, high;
  asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
  return (low | ((uint64_t)high << 32));
}

static inline void load_cr0(u_long data) {
  asm volatile("movq %0, %%cr0" ::"r"(data));
}

static inline u_long rcr0(void) {
  u_long data;
  asm volatile("movq %%cr0, %0" : "=r"(data));
  return data;
}

int memcmp(const void * str1,
  const void * str2, size_t count) {
  const unsigned char * s1 = (const unsigned char * ) str1;
  const unsigned char * s2 = (const unsigned char * ) str2;

  while (count--> 0) {
    if ( * s1++ != * s2++)
      return s1[-1] < s2[-1] ? -1 : 1;
  }
  return 0;
}

static int ksys_open(struct thread * td,
  const char * path, int flags, int mode) {
  int( * sys_open)(struct thread * , struct open_args * ) =
    (void * ) sysents[SYS_open].sy_call;

  td -> td_retval[0] = 0;

  struct open_args uap;
  uap.path = (char * ) path;
  uap.flags = flags;
  uap.mode = mode;
  int error = sys_open(td, & uap);
  if (error)
    return -error;

  return td -> td_retval[0];
}

static int ksys_write(struct thread * td, int fd,
  const void * buf,
    size_t nbytes) {
  int( * sys_write)(struct thread * , struct write_args * ) =
    (void * ) sysents[SYS_write].sy_call;

  td -> td_retval[0] = 0;

  struct write_args uap;
  uap.fd = fd;
  uap.buf = buf;
  uap.nbyte = nbytes;
  int error = sys_write(td, & uap);
  if (error)
    return -error;

  return td -> td_retval[0];
}

static int ksys_close(struct thread * td, int fd) {
  int( * sys_close)(struct thread * , struct close_args * ) =
    (void * ) sysents[SYS_close].sy_call;

  td -> td_retval[0] = 0;

  struct close_args uap;
  uap.fd = fd;
  int error = sys_close(td, & uap);
  if (error)
    return -error;

  return td -> td_retval[0];
}


static int ksys_read(struct thread * td, int fd, void * buf, size_t nbytes) {
  int( * sys_read)(struct thread * , struct read_args * ) =
    (void * ) sysents[SYS_read].sy_call;

  td -> td_retval[0] = 0;

  struct read_args uap;
  uap.fd = fd;
  uap.buf = buf;
  uap.nbyte = nbytes;
  int error = sys_read(td, & uap);
  if (error)
    return -error;

  return td -> td_retval[0];
}

#define SYS_kexec 11

struct sys_kexec_args {
  int( * fptr)(void *,... );
  void * arg;
};

static int sys_kexec(struct thread * td, struct sys_kexec_args * uap) {
  uint64_t kaslr_offset = rdmsr(MSR_LSTAR) - kdlsym_addr_Xfast_syscall;
  int( * printf)(const char * format, ...) = (void * ) kdlsym(printf);
  printf("************ KEXEC CALLED ****************\n", );
  int ret = uap->fptr(td);
  td->td_retval[0] = ret;
  return ret;
}

// https://github.com/SiSTR0/PPPwn/blob/769ab986c40b51fd8fd4df6e40e1311bf07eb1e9/stage2/stage2.c#L198C1-L217C2
void notify(const char *fmt, ...)
{
  uint64_t kaslr_offset = rdmsr(MSR_LSTAR) - kdlsym_addr_Xfast_syscall;
  int (*sceKernelSendNotificationRequest)(int device, OrbisNotificationRequest* req, int size , int blocking) = (void *)kdlsym(sceKernelSendNotificationRequest);
  int (*vsprintf)(char *, const char *, va_list) = (void *)kdlsym(vsprintf);

  OrbisNotificationRequest buf;

  va_list args;
  va_start(args, fmt);
  vsprintf(buf.message, fmt, args);
  va_end(args);

  buf.type = 0;
  buf.unk3 = 0;
  buf.useIconImageUri = 0;
  buf.targetId = -1;

  sceKernelSendNotificationRequest(0, &buf, sizeof(buf), 0);
}

void stage2(void) {
  
  uint64_t kaslr_offset = rdmsr(MSR_LSTAR) - kdlsym_addr_Xfast_syscall;
  uint8_t * kbase = (uint8_t * )(rdmsr(0xC0000082) - 0x1C0);

  uint8_t *kmem = NULL;
  int fd = -1, r = 0;
  int( * printf)(const char * format, ...) = (void * ) kdlsym(printf);
  td = curthread;


  void* buffer = NULL;
  void (*kmem_free)(void* map, void* addr, size_t size) = (void *)(kbase + kmem_free_offset);

  static const int PAYLOAD_SZ = 16 * 1025 * 1024;
  void* (*kmem_alloc)(void*, uint64_t) = (void*)(kbase + kmem_alloc_offset);
  void** kernel_map = (void**)(kbase + kernel_map_offset);
   void * ( * malloc)(unsigned long size, void * type, int flags) = (void * )(kbase + malloc_offset);

  sysents = (struct sysent * ) kdlsym(sysent);
  //
  printf("**********************************   stage2\n");

  // Disable write protection
  uint64_t cr0 = rcr0();
  load_cr0(cr0 & ~CR0_WP);

  // Allow syscalls everywhere
  *(uint32_t * ) kdlsym(amd_syscall_patch1) = 0;
  *(uint16_t * ) kdlsym(amd_syscall_patch2) = 0x9090;
  *(uint16_t * ) kdlsym(amd_syscall_patch3) = 0x9090;
  *(uint8_t * ) kdlsym(amd_syscall_patch4) = 0xeb;

  // Allow user and kernel addresses
  uint8_t nops[] = {
    0x90,
    0x90,
    0x90
  };

  *(uint16_t * ) kdlsym(copyin_patch1) = 0x9090;
  memcpy((void * ) kdlsym(copyin_patch2), nops, sizeof(nops));

  *(uint16_t * ) kdlsym(copyout_patch1) = 0x9090;
  memcpy((void * ) kdlsym(copyout_patch2), nops, sizeof(nops));


  *(uint16_t * ) kdlsym(copyinstr_patch1) = 0x9090;
  memcpy((void * ) kdlsym(copyinstr_patch2), nops, sizeof(nops));
  *(uint16_t * ) kdlsym(copyinstr_patch3) = 0x9090;

  printf("Adding kexec and exploit patches... \n");
  
  //dysym patchea
  kmem = (uint8_t *)&kbase[0x1E4C33]; // Move to offsets.h?
  kmem[0] = 0x90;
 	kmem[1] = 0x90;
 	kmem[2] = 0x90;
 	kmem[3] = 0x90;
 	kmem[4] = 0x90;
	kmem[5] = 0x90;
 
  kmem = (uint8_t *)&kbase[0x1E4C43];
  kmem[0] = 0x90;
  kmem[1] = 0x90;
  kmem[2] = 0x90;
  kmem[3] = 0x90;
  kmem[4] = 0x90;
  kmem[5] = 0x90;
 
  kmem = (uint8_t *)&kbase[0x1E4C63];
  kmem[0] = 0x90;
  kmem[1] = 0xE9;


  // patch MMAP patch 1
  *(uint8_t *)(kbase + 0x015626A) = 0x37;

	// patch MMAP patch 2
  *(uint8_t *)(kbase + 0x015626D)  = 0x37;


  // patch vm_map_protect check
  memcpy((void *)(kbase + 0x35C8EC), "\x90\x90\x90\x90\x90\x90", 6);

//sys_jailbreak

  // Install kexec syscall 11
  struct sysent * sys = & sysents[SYS_kexec];
  sys -> sy_narg = 2;
  sys -> sy_call = (void * ) sys_kexec;
  sys -> sy_thrcnt = 1;

  // Install kexec syscall 11
  sys = & sysents[9];
  sys -> sy_narg = 2;
  sys -> sy_call = (void * ) sys_jailbreak;//sys_rejail
  sys -> sy_thrcnt = 1;

  /*sys = & sysents[110];
  sys -> sy_narg = 2;
  sys -> sy_call = (void * ) sys_rejail;//sys_rejail
  sys -> sy_thrcnt = 1;*/


  printf("kexec & exploit patches added\n");

  // Restore write protection
  load_cr0(cr0);

  fd = ksys_open(td, "/mnt/usb0/loader.bin", O_RDONLY, 0);
  if (fd < 0)
    fd = ksys_open(td, "/mnt/usb1/loader.bin", O_RDONLY, 0);
  if (fd < 0)
    fd = ksys_open(td, "/mnt/usb2/loader.bin", O_RDONLY, 0);
  if (fd < 0)
    fd = ksys_open(td, "/data/loader.bin", O_RDONLY, 0);

  if (fd < 0) {
    printf( "Failed to open loader.bin from local storage\n");
    notify( "Failed to open loader.bin from local storage");
    return;
  }

   // Allocate space for MIRA.
  printf("Allocating memory for MIRA\n");
  buffer = kmem_alloc(*kernel_map, PAYLOAD_SZ);
  if (buffer == NULL) {
    printf(  "Failed to allocate memory for payload\n");
    notify(  "Failed to allocate memory for MIRA");
    return;
  }
  printf("Allocated memory for MIRA\n");


  int payload_size = ksys_read(td, fd, buffer, PAYLOAD_SZ);
  if (payload_size <= 0) {
    printf(  "Failed to read payload\n");
    notify(  "Failed to read payload\n");
    kmem_free(*kernel_map, buffer, PAYLOAD_SZ);
    return;
  }
  ksys_close(td, fd);
  printf("payload_size: %d\n", payload_size);

  // Call entry point if we read the payload.
  if (payload_size)
  {
      void (*entry)(void* args) = (void*)buffer;
      printf("Calling MIRA entry point %p\n", buffer);
      entry(NULL);
      printf("Returned from MIRA entry point\n");
      notify("PPPwn: Successfully launched MIRA");
  }
  else{
     notify("PPPwn: Failed to get MIRA's size");
  }

}
