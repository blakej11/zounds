/*
 * Okay, this is a bit ridiculous that I need to do this, but I haven't found
 * a better way.
 *
 * As described at https://stackoverflow.com/questions/57330890 , GLUT on
 * MacOS 10.14 seems to behave in a way that causes mouse motion to be very
 * jerky. If I update the LC_BUILD_VERSION load command to look like it was
 * compiled on 10.13, the jerky behavior goes away.
 *
 * Sadly I don't know enough about MacOS to understand exactly what downstream
 * behavior changes this tweak causes, but it seems to be the minimum
 * necessary change to make the problematic behavior go away. Thus, this
 * program.
 *
 * The authoritative description of MacOS load commands seems to be
 * <mach-o/loader.h>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <mach-o/loader.h>

/* "10.13", following the encoding convention described in <mach-o/loader.h> */
#define	MACOS_HIGH_SIERRA	0x000a0d00

int
main(int argc, char **argv)
{
	const char	*filename = NULL;
	bool		verbose = false;
	bool		found = false;
	struct stat	st;
	int		fd;
	char		*addr, *p;

	if (argc == 3 && strcmp(argv[1], "-v") == 0) {
		verbose = true;
		filename = argv[2];
	} else if (argc == 2) {
		filename = argv[1];
	}
	if (filename == NULL || (fd = open(filename, O_RDWR)) == -1) {
		fprintf(stderr, "Usage: %s [-v] <filename>\n", argv[0]);
		return (1);
	}

	if (fstat(fd, &st) != 0) {
		perror("fstat");
		return (1);
	}
	addr = mmap(NULL, st.st_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		return (1);
	}
	p = addr + sizeof (struct mach_header_64);

	while (!found) {
		struct load_command	*lc = (struct load_command *)p;
		void		*const	pageaddr =
		    (void *)(((uintptr_t)p) & ~0x1fff);

		if (verbose) {
			printf("found load command 0x%02x\n", lc->cmd);
		}
		if (lc->cmd == 0) {
			break;	// no more load commands
		} else if (lc->cmd == LC_VERSION_MIN_MACOSX) {
			struct version_min_command	*vm = 
			    (struct version_min_command *)p;

			if (verbose) {
				printf("found LC_VERSION_MIN_MACOSX "
				    "load command at offset 0x%ld\n", p - addr);
				printf("\t(version = 0x%08x, sdk = 0x%08x)\n",
				    vm->version, vm->sdk);
			}

			if (vm->sdk > MACOS_HIGH_SIERRA) {
				// Do it.
				vm->sdk = MACOS_HIGH_SIERRA;

				msync(pageaddr, 0x2000, MS_SYNC);
				if (verbose) {
					printf("updating \"sdk\" field to "
					    "0x%08x\n", MACOS_HIGH_SIERRA);
				}
			} else {
				if (verbose) {
					printf("\"sdk\" field is just 0x%08x, "
					    "should be fine\n", vm->sdk);
				}
			}
			found = true;
		} else if (lc->cmd == LC_BUILD_VERSION) {
			struct build_version_command	*bv = 
			    (struct build_version_command *)p;

			if (verbose) {
				printf("found LC_BUILD_VERSION load command "
				    "at offset 0x%ld\n", p - addr);
				printf("\t(minos = 0x%08x, sdk = 0x%08x)\n",
				    bv->minos, bv->sdk);
			}

			if (bv->sdk > MACOS_HIGH_SIERRA) {
				// Do it.
				bv->sdk = MACOS_HIGH_SIERRA;

				msync(pageaddr, 0x2000, MS_SYNC);
				if (verbose) {
					printf("updating \"sdk\" field to "
					    "0x%08x\n", MACOS_HIGH_SIERRA);
				}
				found = true;
			} else {
				if (verbose) {
					printf("\"sdk\" field is just 0x%08x, "
					    "should be fine\n", bv->sdk);
				}
			}
			found = true;
		}
		p += lc->cmdsize;
	}

	close(fd);

	if (!found) {
		fprintf(stderr, "Failed to find either LC_VERSION_MIN_MACOSX "
		    "or LC_BUILD_VERSION load commands;\n");
		fprintf(stderr, "GLUT bug may be present! "
		    "(https://stackoverflow.com/questions/57330890)\n");
	}
	return (found ? 0 : 1);
}
