/*
 * Convert 32-bit (i386) minicom phonebooks (~/.dialdir) for use on 64-bit
 * (amd64) platforms. Should be runnable on either i386 or amd64, but
 * the conversion only goes one way (32- -> 64-bit).
 *
 * Copyright (c) 2011, John Morrissey <jwm@horde.net>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
  int fd, ret;
  char data[65535], dialdir_bak_name[MAXPATHLEN],
    dialdir_padding[] = {'\0', '\0', '\0', '\0'};
  unsigned short old_dialent_size = 0;
  ssize_t read_len, write_len, d_offset;
  struct stat st;

  if (argc != 2) {
    printf("Usage: %s DIALDIR\n", argv[0]);
    return 1;
  }

  ret = stat(argv[1], &st);
  if (ret == -1) {
    printf("stat(%s): %s\n", argv[1], strerror(errno));
    return 1;
  }

  fd = open(argv[1], O_RDONLY);
  if (fd == -1) {
    printf("open(%s): %s\n", argv[1], strerror(errno));
    return 1;
  }

  read_len = read(fd, data, sizeof(data));
  if (read_len == -1) {
    printf("read(%s): %s\n", argv[1], strerror(errno));
    return 1;
  }

  if (read_len != st.st_size) {
    printf("short read(%s): stat() == %lu vs. read length %lu.\n",
      argv[1], st.st_size, read_len);
    return 1;
  }

  ret = scnprintf(dialdir_bak_name, sizeof(dialdir_bak_name), "%s.i386",
    argv[1]);
  if (ret != strlen(argv[1]) + strlen(".i386")) {
    printf("overflow when trying to back up %s to %s.i386.",
      argv[1], argv[1]);
    return 1;
  }
  ret = rename(argv[1], dialdir_bak_name);
  if (ret == -1) {
    printf("rename(%s, %s): %s\n", argv[1], dialdir_bak_name,
      strerror(errno));
    return 1;
  }

  /* Bytes 3,4 are the dial directory version. */
  switch (data[2]) {
  case 1:
  case 2:
  case 3:
  case 4:
  case 5:
    /* Versions one through five need the same treatment when
     * moving from i386 -> amd64.
     */
    break;

  default:
    printf("unknown minicom phonebook version: %d\n", data[2]);
    return 1;
  }

  /* First 14 bytes are a struct dver. Update its size member to
   * reflect the larger struct size on amd64.
   */
  old_dialent_size = (data[5] & 0x00ff) + ((data[4] << 8) & 0xff00);
  data[5] += 4;

  /* Subsequent blocks of old_dialent_size bytes are struct dialents,
   * which need to be null padded with four bytes to account for
   * the larger linked list pointer in each struct dialent.
   */
  fd = open(argv[1], O_CREAT | O_EXCL | O_WRONLY, S_IRUSR | S_IWUSR);
  if (fd == -1) {
    printf("open(%s) for writing: %s\n", argv[1], strerror(errno));
    return 1;
  }

  d_offset = 14;

  ret = write(fd, data, d_offset);
  if (ret != d_offset) {
    printf("short write(%s): wanted to write %lu but only wrote %d.\n",
      argv[1], d_offset, ret);
    return 1;
  }

  while (d_offset < read_len) {
    ret = write(fd, data + d_offset, old_dialent_size);
    if (ret != old_dialent_size) {
      printf("short write(%s): wanted to write %d but only wrote %d.\n",
        argv[1], old_dialent_size, ret);
      return 1;
    }

    write_len = write(fd, dialdir_padding, sizeof(dialdir_padding));
    if (write_len != sizeof(dialdir_padding)) {
      printf("short write(%s): wanted to write %lu but only wrote %lu.\n",
        argv[1], sizeof(dialdir_padding), write_len);
      return 1;
    }
    
    d_offset += old_dialent_size;
  }

  close(fd);
  return 0;
}
