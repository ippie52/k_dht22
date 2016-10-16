/*------------------------------------------------------------------------------
 *! \file   locking.c
 *! \brief  Prevents multiple access to the same index by creating a locking
 *          file, which is removed upon successful exit.
 *
 *  This was originally obtained from https://github.com/technion/lol_dht22, by
 *  technion@lolware.net
 *------------------------------------------------------------------------------
 *                   Kris Dunning ippie52@gmail.com 2016.
 *------------------------------------------------------------------------------
 */

#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "locking.h"

/*******************************************************************************
 *  \brief  Gets the string name of the lock file, taking the sensor pin into
 *          account.
 *  \return The length of the string.
 */
int get_lockfile_name
(
   const int sensor,  /*!<IN  - The sensor ID                     */
   char *buffer,      /*!<OUT - The string buffer to write to     */
   const int size     /*!<IN  - The maximum length of the string  */
)
{
    return snprintf(buffer, size, "/var/run/dht%d.lock", sensor);
}

/*******************************************************************************
 *  \brief  Opens the lock file at the given file name and returns the file
 *          descriptor.
 *  \return The file descriptor of the lock file.
 */
int open_lockfile
(
   const char *filename    /*!<IN - The file name of the lock file to create  */
)
{
   int fd;
   fd = open(filename, O_CREAT | O_RDONLY, 0600);

   if (fd < 0)
   {
      printf("Failed to access lock file: %s\nerror: %s\n",
		filename, strerror(errno));
      exit(EXIT_FAILURE);
   }

   while(flock(fd, LOCK_EX | LOCK_NB) == -1)
   {
      if(errno == EWOULDBLOCK)
      {
         printf("Lock file is in use, exiting...\n");
         /* If the lock file is in use, we COULD sleep and try again.
          * However, a lock file would more likely indicate an already runaway
	       * process. */
	     exit(EXIT_FAILURE);
      }
      perror("Flock failed");
      exit(EXIT_FAILURE);
   }
   return fd;
}

/*******************************************************************************
 *  \brief  Closes the given lock file.
 */
void close_lockfile
(
   const int fd   /*!<IN - The file descriptor of the lock file to close   */
)
{
   if(flock(fd, LOCK_UN) == -1)
   {
      perror("Failed to unlock file");
      exit(EXIT_FAILURE);
   }
   if(close(fd) == -1)
   {
      perror("Closing descriptor on lock file failed");
      exit(EXIT_FAILURE);
   }
}
