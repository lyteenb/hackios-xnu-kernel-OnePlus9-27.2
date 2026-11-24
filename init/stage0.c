/*
 * /sbin/launchd of the big system image: the first program the kernel runs.
 *
 * The system image (the Darwin files and the dyld shared cache) takes a minute
 * and a half to upload to the phone, so nothing that changes often lives in
 * it. All of that - the real init, its tests, the list of launchd jobs - is
 * in the small image that travels with the kernel (the second RAM disk,
 * /dev/md1), an APFS container (tools/apfs/mkapfs.py). This program
 * mounts it on /hakios and hands over.
 */
#include "mini.h"

int
main(int argc, char **argv, char **envp)
{
	char *init_argv[] = { "/hakios/sbin/init", NULL };
	char *init_envp[] = { NULL };
	int fd = open("/dev/console", O_RDWR, 0);

	(void)argc, (void)argv, (void)envp;
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	if (fd > 2) {
		close(fd);
	}

	/*
	 * The system image is the root (md0), and the Preboot volume is its second
	 * volume.  Darwin 27's launchd/libignition asks the root for the Preboot
	 * device through an APFS fsctl, then expects the volume to be mounted at
	 * /private/preboot before it can run its next stage.  Do that here, before
	 * handing over to init; a system image whose Preboot is elsewhere may not
	 * have this volume, and init's own /private/preboot mount is the fallback.
	 */
	if (mount("apfs", "/private/preboot", MNT_RDONLY, (void *)"/dev/md0,v=1") != 0) {
		printf("stage0: cannot mount /dev/md0,v=1 on /private/preboot: %s\n", strerror(errno));
	}
	if (mount("apfs", "/hakios", MNT_RDONLY, (void *)"/dev/md1") != 0) {
		printf("stage0: cannot mount /dev/md1 on /hakios: %s\n", strerror(errno));
	} else {
		/* Darwin 27's first process does not hand on a usable envp pointer. */
		execve(init_argv[0], init_argv, init_envp);
		printf("stage0: cannot run %s: %s\n", init_argv[0], strerror(errno));
	}
	reboot(0x08 /* RB_HALT */);
	return 1;
}
