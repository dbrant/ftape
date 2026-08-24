/*
 *      Copyright (C) 2026 Dmitry Brant.
 *
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 *      ftapecmd: send a bare QIC-117 command to a tape drive, using the
 *      MTIOCFTCMD ioctl of the ftape driver.
 *
 *      Example: ftapecmd -f /dev/nrawqft0 -c 20
 *               ftapecmd -c "report drive status" -b 8
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/mtio.h>
#include <linux/qic117.h>

#define DEFAULT_DEVICE "/dev/nrawqft0"

#define NR_ITEMS(x) (sizeof(x) / sizeof((x)[0]))

/*  the QIC-117 command and error tables live in the driver headers,
 *  so that this utility never gets out of sync with the driver.
 */
static const struct qic117_command_table qic_cmds[] = QIC117_COMMANDS;
static const ftape_error qic_errors[] = QIC117_ERRORS;

static const char *type_name(unsigned int cmd_type)
{
	switch (cmd_type) {
	case unused: return "unused";
	case mode:   return "mode";
	case motion: return "motion";
	case report: return "report";
	}
	return "?";
}

/*  reduce a command name to lowercase letters and digits only, so that
 *  "report drive status", "report-drive-status" and "ReportDriveStatus"
 *  all compare equal.
 */
static void normalize(const char *src, char *dst, size_t dst_sz)
{
	size_t n = 0;

	for (; *src != '\0' && n + 1 < dst_sz; src++) {
		if (isalnum((unsigned char)*src)) {
			dst[n++] = (char)tolower((unsigned char)*src);
		}
	}
	dst[n] = '\0';
}

/*  look up a command by name: exact match first, then a unique substring
 *  match. Returns the QIC-117 command number, or -1 / -2 (ambiguous).
 */
static int lookup_command(const char *name)
{
	char wanted[128];
	char have[128];
	int match = -1;
	unsigned int i;

	normalize(name, wanted, sizeof(wanted));
	if (wanted[0] == '\0') {
		return -1;
	}
	for (i = 0; i < NR_ITEMS(qic_cmds); i++) {
		if (qic_cmds[i].name == NULL) {
			continue;
		}
		normalize(qic_cmds[i].name, have, sizeof(have));
		if (strcmp(have, wanted) == 0) {
			return (int)i;
		}
	}
	for (i = 0; i < NR_ITEMS(qic_cmds); i++) {
		if (qic_cmds[i].name == NULL) {
			continue;
		}
		normalize(qic_cmds[i].name, have, sizeof(have));
		if (strstr(have, wanted) != NULL) {
			if (match >= 0) {
				return -2; /* ambiguous */
			}
			match = (int)i;
		}
	}
	return match;
}

/*  parse a number, accepting decimal, 0x hex and 0 octal
 */
static int parse_num(const char *str, unsigned long *result)
{
	char *end;
	unsigned long val;

	errno = 0;
	val = strtoul(str, &end, 0);
	if (errno != 0 || end == str || *end != '\0') {
		return -1;
	}
	*result = val;
	return 0;
}

/*  parse a comma (or space) separated list of drive parameters
 */
static int parse_parms(const char *str, unsigned char *parms, unsigned int max)
{
	unsigned int count = 0;
	const char *p = str;

	while (*p != '\0') {
		char *end;
		unsigned long val;

		while (*p == ',' || isspace((unsigned char)*p)) {
			p++;
		}
		if (*p == '\0') {
			break;
		}
		errno = 0;
		val = strtoul(p, &end, 0);
		if (errno != 0 || end == p) {
			fprintf(stderr, "ftapecmd: bad parameter list: %s\n", str);
			return -1;
		}
		if (val > 15) {
			fprintf(stderr, "ftapecmd: parameter %lu out of range "
				"(QIC-117 parameters are nibbles, 0-15)\n", val);
			return -1;
		}
		if (count >= max) {
			fprintf(stderr, "ftapecmd: at most %u parameters "
				"may be sent\n", max);
			return -1;
		}
		parms[count++] = (unsigned char)val;
		p = end;
	}
	if (count == 0) {
		fprintf(stderr, "ftapecmd: empty parameter list\n");
		return -1;
	}
	return (int)count;
}

static void print_status_bits(unsigned int status)
{
	static const struct {
		unsigned int bit;
		const char *name;
	} bits[] = {
		{ QIC_STATUS_READY,             "ready" },
		{ QIC_STATUS_ERROR,             "error" },
		{ QIC_STATUS_CARTRIDGE_PRESENT, "cartridge-present" },
		{ QIC_STATUS_WRITE_PROTECT,     "write-protected" },
		{ QIC_STATUS_NEW_CARTRIDGE,     "new-cartridge" },
		{ QIC_STATUS_REFERENCED,        "referenced" },
		{ QIC_STATUS_AT_BOT,            "at-BOT" },
		{ QIC_STATUS_AT_EOT,            "at-EOT" },
	};
	unsigned int i;
	int first = 1;

	if ((status & 0xff) == 0) {
		printf("<none>");
		return;
	}
	for (i = 0; i < NR_ITEMS(bits); i++) {
		if (status & bits[i].bit) {
			printf("%s%s", first ? "" : ", ", bits[i].name);
			first = 0;
		}
	}
}

static const char *error_message(unsigned int error)
{
	if (error < NR_ITEMS(qic_errors)) {
		return qic_errors[error].message;
	}
	return "Unknown error";
}

static const char *command_name(unsigned int cmd)
{
	if (cmd < NR_ITEMS(qic_cmds) && qic_cmds[cmd].name != NULL) {
		return qic_cmds[cmd].name;
	}
	return "unknown command";
}

static void list_commands(void)
{
	unsigned int i;

	printf("QIC-117 commands known to the driver:\n\n");
	printf(" cmd  type    name\n");
	printf(" ---  ------  ----\n");
	for (i = 0; i < NR_ITEMS(qic_cmds); i++) {
		if (qic_cmds[i].name == NULL) {
			continue;
		}
		printf(" %3u  %-6s  %s%s\n", i,
		       type_name(qic_cmds[i].cmd_type),
		       qic_cmds[i].name,
		       qic_cmds[i].non_intr ? " (non-interruptible)" : "");
	}
	printf("\nCommands of type \"report\" return data: pass -b with the "
	       "number of bits to\nread back (usually 8, or 16 for "
	       "\"report error code\").\n");
}

static void usage(FILE *out)
{
	fprintf(out,
"Usage: ftapecmd [options] -c <command>\n"
"\n"
"Send a bare QIC-117 command to a floppy tape drive, via the MTIOCFTCMD\n"
"ioctl of the ftape driver. The driver must be in raw mode, which is the\n"
"case for the raw device nodes (/dev/rawqft*, /dev/nrawqft*), or use -r.\n"
"\n"
"Options:\n"
"  -f, --device <dev>    tape device (default: " DEFAULT_DEVICE ",\n"
"                        overridden by $FTAPE_DEV)\n"
"  -c, --command <cmd>   command to send, as a number (e.g. 20, 0x14) or as\n"
"                        a name (e.g. \"report drive status\"). Required.\n"
"  -p, --parms <n,...>   up to 3 parameters (nibbles, 0-15) to send after\n"
"                        the command\n"
"  -b, --bits <n>        number of result bits to read back from the drive\n"
"                        (for \"report\" commands; 8 or 16 typically)\n"
"  -w, --wait-before <n> wait up to n milliseconds for the drive to become\n"
"                        ready before sending the command\n"
"  -a, --wait-after <n>  wait up to n milliseconds for the drive to become\n"
"                        ready after sending the command\n"
"  -r, --raw             switch the driver into raw mode first (MTIOCFTMODE)\n"
"  -l, --list            list the known QIC-117 commands and exit\n"
"  -n, --dry-run         print what would be sent, without touching the drive\n"
"  -q, --quiet           print only the result value (and errors)\n"
"  -v, --verbose         print the full ioctl argument before and after\n"
"  -h, --help            this help\n"
"\n"
"Exit status: 0 on success, 1 on usage error, 2 if the ioctl failed, 3 if\n"
"the drive reported an error condition.\n"
"\n"
"Examples:\n"
"  ftapecmd -f /dev/nrawqft0 -c 20\n"
"  ftapecmd -c \"report drive status\" -b 8\n"
"  ftapecmd -c 6 -b 8 -w 5000\n"
"  ftapecmd -c \"seek head to track\" -p 3 -w 1000 -a 10000\n");
}

int main(int argc, char **argv)
{
	static const struct option long_opts[] = {
		{ "device",      required_argument, NULL, 'f' },
		{ "command",     required_argument, NULL, 'c' },
		{ "parms",       required_argument, NULL, 'p' },
		{ "bits",        required_argument, NULL, 'b' },
		{ "wait-before", required_argument, NULL, 'w' },
		{ "wait-after",  required_argument, NULL, 'a' },
		{ "raw",         no_argument,       NULL, 'r' },
		{ "list",        no_argument,       NULL, 'l' },
		{ "dry-run",     no_argument,       NULL, 'n' },
		{ "quiet",       no_argument,       NULL, 'q' },
		{ "verbose",     no_argument,       NULL, 'v' },
		{ "help",        no_argument,       NULL, 'h' },
		{ NULL,          0,                 NULL,  0  }
	};
	const char *device = getenv("FTAPE_DEV");
	unsigned char parms[3];
	unsigned long value;
	struct mtftcmd cmd;
	struct mtftmode ftmode;
	int command = -1;
	int parm_cnt = 0;
	unsigned int bits = 0;
	unsigned int wait_before = 0;
	unsigned int wait_after = 0;
	int set_raw = 0;
	int dry_run = 0;
	int quiet = 0;
	int verbose = 0;
	int status = 0;
	int fd;
	int c;

	if (device == NULL || device[0] == '\0') {
		device = DEFAULT_DEVICE;
	}
	memset(parms, 0, sizeof(parms));

	while ((c = getopt_long(argc, argv, "f:c:p:b:w:a:rlnqvh",
				long_opts, NULL)) != -1) {
		switch (c) {
		case 'f':
			device = optarg;
			break;
		case 'c':
			if (parse_num(optarg, &value) == 0) {
				if (value >= NR_ITEMS(qic_cmds)) {
					fprintf(stderr, "ftapecmd: command %lu "
						"out of range (0-%u)\n", value,
						(unsigned int)NR_ITEMS(qic_cmds) - 1);
					return 1;
				}
				command = (int)value;
			} else {
				command = lookup_command(optarg);
				if (command == -2) {
					fprintf(stderr, "ftapecmd: ambiguous "
						"command name: %s\n", optarg);
					return 1;
				}
				if (command < 0) {
					fprintf(stderr, "ftapecmd: unknown "
						"command: %s  (try --list)\n",
						optarg);
					return 1;
				}
			}
			break;
		case 'p':
			parm_cnt = parse_parms(optarg, parms, NR_ITEMS(parms));
			if (parm_cnt < 0) {
				return 1;
			}
			break;
		case 'b':
			if (parse_num(optarg, &value) != 0 || value > 32) {
				fprintf(stderr, "ftapecmd: bad number of "
					"result bits: %s\n", optarg);
				return 1;
			}
			bits = (unsigned int)value;
			break;
		case 'w':
			if (parse_num(optarg, &value) != 0) {
				fprintf(stderr, "ftapecmd: bad timeout: %s\n",
					optarg);
				return 1;
			}
			wait_before = (unsigned int)value;
			break;
		case 'a':
			if (parse_num(optarg, &value) != 0) {
				fprintf(stderr, "ftapecmd: bad timeout: %s\n",
					optarg);
				return 1;
			}
			wait_after = (unsigned int)value;
			break;
		case 'r':
			set_raw = 1;
			break;
		case 'l':
			list_commands();
			return 0;
		case 'n':
			dry_run = 1;
			break;
		case 'q':
			quiet = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'h':
			usage(stdout);
			return 0;
		default:
			usage(stderr);
			return 1;
		}
	}
	if (optind < argc) {
		fprintf(stderr, "ftapecmd: unexpected argument: %s\n",
			argv[optind]);
		usage(stderr);
		return 1;
	}
	if (command < 0) {
		fprintf(stderr, "ftapecmd: no command given\n");
		usage(stderr);
		return 1;
	}
	if (command == QIC_NO_COMMAND) {
		fprintf(stderr, "ftapecmd: 0 is not a QIC-117 command "
			"(try --list)\n");
		return 1;
	}
	if (bits != 0 && parm_cnt > 0) {
		fprintf(stderr, "ftapecmd: warning: the driver ignores "
			"parameters when result bits are requested\n");
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.ft_wait_before = wait_before;
	cmd.ft_cmd         = (qic117_cmd_t)command;
	cmd.ft_parm_cnt    = (unsigned char)parm_cnt;
	memcpy(cmd.ft_parms, parms, sizeof(cmd.ft_parms));
	cmd.ft_result_bits = bits;
	cmd.ft_wait_after  = wait_after;

	if (!quiet) {
		printf("command %d (%s), type %s\n", command,
		       command_name(command),
		       type_name(qic_cmds[command].cmd_type));
		if (qic_cmds[command].cmd_type == unused) {
			printf("note: this command is reserved, undefined or "
			       "vendor unique in QIC-117;\n"
			       "      sending it anyway.\n");
		}
		if (qic_cmds[command].cmd_type == report && bits == 0) {
			printf("note: this is a report command, but no result "
			       "bits were requested (-b).\n");
		}
	}
	if (verbose || dry_run) {
		int i;

		printf("  device:       %s\n", device);
		printf("  wait_before:  %u ms\n", cmd.ft_wait_before);
		printf("  parm_cnt:     %u", cmd.ft_parm_cnt);
		for (i = 0; i < parm_cnt; i++) {
			printf("%s%u", i == 0 ? " [" : ", ", cmd.ft_parms[i]);
		}
		printf("%s\n", parm_cnt > 0 ? "]" : "");
		printf("  result_bits:  %u\n", cmd.ft_result_bits);
		printf("  wait_after:   %u ms\n", cmd.ft_wait_after);
		printf("  raw mode:     %s\n",
		       set_raw ? "requested via MTIOCFTMODE" : "not requested");
	}
	if (dry_run) {
		printf("dry run: nothing sent\n");
		return 0;
	}

	fd = open(device, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "ftapecmd: cannot open %s: %s\n",
			device, strerror(errno));
		return 2;
	}
	if (set_raw) {
		memset(&ftmode, 0, sizeof(ftmode));
		/*  ft_rawmode is a *signed* one-bit bitfield, so the only
		 *  non-zero value that fits into it is -1. The driver only
		 *  tests it for being non-zero.
		 */
		ftmode.ft_rawmode = -1;
		if (ioctl(fd, MTIOCFTMODE, &ftmode) < 0) {
			fprintf(stderr, "ftapecmd: MTIOCFTMODE failed: %s\n",
				strerror(errno));
			close(fd);
			return 2;
		}
	}
	if (ioctl(fd, MTIOCFTCMD, &cmd) < 0) {
		fprintf(stderr, "ftapecmd: MTIOCFTCMD failed: %s\n",
			strerror(errno));
		if (errno == EACCES) {
			fprintf(stderr, "ftapecmd: the driver needs to be in "
				"raw mode for this ioctl: use a raw device "
				"node\n            (%s) or pass -r.\n",
				DEFAULT_DEVICE);
		}
		close(fd);
		return 2;
	}
	close(fd);

	if (verbose) {
		printf("  ft_result:    0x%08x\n", cmd.ft_result);
		printf("  ft_status:    0x%08x\n", (unsigned int)cmd.ft_status);
		printf("  ft_error:     %d\n", cmd.ft_error);
	}
	if (bits != 0) {
		if (quiet) {
			printf("0x%x\n", cmd.ft_result);
		} else {
			printf("result: 0x%0*x (%u)\n", (int)((bits + 3) / 4),
			       cmd.ft_result, cmd.ft_result);
		}
		if (!quiet && command == QIC_REPORT_DRIVE_STATUS) {
			printf("        ");
			print_status_bits(cmd.ft_result);
			printf("\n");
		}
		if (!quiet && command == QIC_REPORT_ERROR_CODE) {
			printf("        error %u: %s\n",
			       cmd.ft_result & 0xff,
			       error_message(cmd.ft_result & 0xff));
			printf("        caused by command %u (%s)\n",
			       (cmd.ft_result >> 8) & 0xff,
			       command_name((cmd.ft_result >> 8) & 0xff));
		}
	}
	if (wait_before != 0 || wait_after != 0) {
		if (!quiet) {
			printf("status: 0x%02x (", cmd.ft_status & 0xff);
			print_status_bits((unsigned int)cmd.ft_status);
			printf(")\n");
		}
	}
	if (cmd.ft_status & QIC_STATUS_ERROR) {
		fprintf(stderr, "ftapecmd: drive reported error %d: %s%s\n",
			cmd.ft_error, error_message((unsigned int)cmd.ft_error),
			((unsigned int)cmd.ft_error < NR_ITEMS(qic_errors) &&
			 qic_errors[cmd.ft_error].fatal) ? " (fatal)" : "");
		status = 3;
	} else if (!quiet && bits == 0 &&
		   wait_before == 0 && wait_after == 0) {
		printf("sent.\n");
	}
	return status;
}
