#!/usr/bin/env python3
#
# Copyright 2018-present Facebook. All Rights Reserved.
#
# This program file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the
# Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program in a file named COPYING; if not, write to the
# Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor,
# Boston, MA 02110-1301 USA
#
import functools
import re
import subprocess
import sys
from unittest import TestCase


class BusyBoxSyntaxTest(TestCase):
    # Parameterised test, will run for every entry in KNOWN_USAGE_STRINGS
    def _test_usage_string_template(self, cmd, expected_strings):
        """
        For each busybox command, check if its --help output is known
        """
        found_usage_strings = self.get_cmd_usage_strings(cmd)

        self.assertTrue(
            bool(found_usage_strings),
            "could not find usage strings from `{cmd} --help`".format(cmd=cmd),
        )
        self.assertTrue(
            any(usage in expected_strings for usage in found_usage_strings),
            "Unknown usage from command `{cmd} --help`: {found_usage_strings}".format(
                cmd=cmd, found_usage_strings=found_usage_strings
            ),
        )

    ## Utils
    def get_cmd_usage_strings(self, cmd):
        "Runs `{cmd} --help` and extracts usage strings"
        output = subprocess.check_output(
            "{cmd} --help || true".format(cmd=cmd),
            stderr=subprocess.STDOUT,
            shell=True,
            timeout=10,
        )

        return [
            m.group("syntax").strip()
            for m in re.finditer(
                r"^\s*(Usage:|{cmd}:)?\s*(?P<syntax>{cmd}.*)".format(cmd=cmd),
                output.decode("utf-8"),
                flags=re.MULTILINE,
            )
        ]

    def test_busybox_usage_strings(self):
        for cmd, expected_strings in KNOWN_USAGE_STRINGS.items():
            self._test_usage_string_template(cmd, expected_strings)


# Initial set generated using:
# $ for cmd in $(ls -l /bin /usr/bin | grep -Eo '[^ ]+ -> /bin/busybox' | grep -Eo '^[^ ]+'); do echo "'$cmd': [\""$($cmd --help 2>&1 | grep -E "^Usage: $cmd |^$cmd"] | sed "s/^Usage: //")"\"],"; done | sort  # noqa: B950

KNOWN_USAGE_STRINGS = {
    "ash": [
        "ash [-/+OPTIONS] [-/+o OPT]... [-c 'SCRIPT' [ARG0 [ARGS]] / FILE [ARGS]]",
        "ash [-/+OPTIONS] [-/+o OPT]... [-c 'SCRIPT' [ARG0 [ARGS]] / FILE [ARGS] / -s [ARGS]]",  # BusyBox 1.30.1  # noqa: B950
        "ash [-il] [-|+Cabefmnuvx] [-|+o OPT]... [-c 'SCRIPT' [ARG0 ARGS] | FILE ARGS | -s ARGS]",  # BusyBox v1.35.0
    ],
    "awk": ["awk [OPTIONS] [AWK_PROGRAM] [FILE]..."],
    "base64": [
        "base64 [-d] [FILE]",
        "base64 [OPTION]... [FILE]",
        "base64 [-d] [-w COL] [FILE]",  # BusyBox v1.35.0
    ],
    "basename": [
        "basename FILE [SUFFIX]",
        "basename FILE [SUFFIX] | -a FILE... | -s SUFFIX FILE...",  # BusyBox v1.35.0
    ],
    "bunzip2": [
        "bunzip2 [-cf] [FILE]...",
        "bunzip2 [-cfk] [FILE]...",  # BusyBox 1.30.1
    ],
    "bzcat": ["bzcat [FILE]..."],
    "cat": ["cat [FILE]...", "cat [-nb] [FILE]..."],  # BusyBox 1.30.1
    "chattr": [
        "chattr [-R] [-+=AacDdijsStTu] [-v VERSION] [FILE]...",
        "chattr [-pRVf] [-+=aAcCdDeijPsStTu] [-v version] files...",  # BusyBox 1.30.1
        "chattr [-pRVf] [-+=aAcCdDeijPsStTuF] [-v version] files...",  # BusyBox 1.31.0
        "chattr [-pRVf] [-+=aAcCdDeijPsStTuFx] [-v version] files...",
        "chattr [-R] [-v VERSION] [-+=AacDdijsStTu] FILE...",  # BusyBox 1.31.1
        "chattr [-R] [-v VERSION] [-p PROJID] [-+=AacDdijsStTu] FILE...",  # BusyBox v1.35.0
        "chattr [-RVf] [-+=aAcCdDeijPsStTuFx] [-p project] [-v version] files...",
    ],
    "chgrp": [
        "chgrp [-RhLHP]... GROUP FILE...",
        "chgrp [-Rh]... GROUP FILE...",  # BusyBox v1.35.0
    ],
    "chmod": ["chmod [-R] MODE[,MODE]... FILE..."],
    "chown": [
        "chown [-Rh]... OWNER[<.|:>[GROUP]] FILE...",
        "chown [-Rh]... USER[:[GRP]] FILE...",  # BusyBox 1.30.1
    ],
    "chpst": ["chpst [-vP012] [-u USER[:GRP]] [-U USER[:GRP]] [-e DIR]"],
    "chvt": ["chvt N"],
    # fmt: off
    "cksum": [
        "cksum FILES...",
        "cksum FILE...",  # BusyBox 1.30.1
    ],
    # fmt: on
    "clear": ["clear"],
    "cmp": [
        "cmp [-l] [-s] FILE1 [FILE2]",
        "cmp [-ls] [-n NUM] FILE1 [FILE2]",  # BusyBox v1.35.0
        "cmp [-l|s] [-n NUM] FILE1 [FILE2]",  # BusyBox v1.38.0
    ],
    "cp": [
        "cp [OPTIONS] SOURCE... DEST",
        "cp [-arPLHpfinlsTu] SOURCE DEST",  # BusyBox v1.35.0
    ],
    "cpio": [
        "cpio [-dmvu] [-F FILE] [-ti] [EXTR_FILE]...",
        "cpio [-dmvu] [-F FILE] [-R USER[:GRP]] [-ti] [EXTR_FILE]...",  # BusyBox 1.30.1
    ],
    "crontab": ["crontab [-c DIR] [-u USER] [-ler]|[FILE]"],
    "cut": [
        "cut [OPTIONS] [FILE]...",
        "cut {-b|c LIST | -f|F LIST [-d SEP] [-s]} [-D] [-O SEP] [FILE]...",  # BusyBox 1.38.0
    ],
    "date": [
        "date [OPTIONS] [+FMT] [TIME]",
        "date [OPTIONS] [+FMT] [[-s] TIME]",  # BusyBox v1.35.0
    ],
    "dc": [
        "dc EXPRESSION...",  # noqa: B950
        "dc [-eSCRIPT]... [-fFILE]... [FILE]...",  # BusyBox 1.30.1
    ],
    "dd": [
        "dd [if=FILE] [of=FILE] [bs=N] [count=N] [skip=N]",  # noqa: B950
        "dd [if=FILE] [of=FILE] [bs=N] [count=N] [skip=N] [seek=N]",  # BusyBox 1.30.1
    ],
    "deallocvt": ["deallocvt [N]"],
    "df": [
        "df [-PkmhT] [FILESYSTEM]...",
        "df [-PkmhT] [-t TYPE] [FILESYSTEM]...",  # BusyBox v1.35.0
    ],
    "diff": ["diff [-abBdiNqrTstw] [-L LABEL] [-S FILE] [-U LINES] FILE1 FILE2"],
    "dirname": ["dirname FILENAME"],
    "dmesg": [
        "dmesg [-c] [-n LEVEL] [-s SIZE]",  # noqa: B950
        "dmesg [options]",  # BusyBox 1.30.1
        "dmesg [-cr] [-n LEVEL] [-s SIZE]",  # BusyBox v1.35.0
    ],
    "du": ["du [-aHLdclsxhmk] [FILE]..."],
    "dumpkmap": ["dumpkmap > keymap"],
    "env": [
        "env [-iu] [-] [name=value]... [PROG ARGS]",
        "env [-i0] [-u NAME]... [-] [NAME=VALUE]... [PROG ARGS]",  # BusyBox v1.35.0
    ],
    "envdir": ["envdir DIR PROG ARGS"],
    "envuidgid": ["envuidgid USER PROG ARGS"],
    "expr": ["expr EXPRESSION"],
    "find": ["find [-HL] [PATH]... [OPTIONS] [ACTIONS]"],
    "flock": [
        "flock [-sxun] FD|{FILE [-c] PROG ARGS}",
        "flock [options] <file>|<directory> <command> [<argument>...]",  # BusyBox 1.30.1  # noqa: B950
        "flock [-sxun] FD | { FILE [-c] PROG ARGS }",  # BusyBox v1.35.0
    ],
    "free": ["free"],
    "fuser": [
        "fuser [OPTIONS] FILE or PORT/PROTO",
        "fuser [-msk46] [-SIGNAL] FILE or PORT/PROTO",  # BusyBox v1.35.0
    ],
    "getopt": [
        "getopt [OPTIONS] [--] OPTSTRING PARAMS",
        "getopt <optstring> <parameters>",  # BusyBox 1.30.1
    ],
    "grep": [
        "grep [-HhnlLoqvsriwFE] [-m N] [-A/B/C N] PATTERN/-e PATTERN.../-f FILE [FILE]...",  # noqa: B950
        "grep [-HhnlLoqvsrRiwFE] [-m N] [-A|B|C N] { PATTERN | -e PATTERN... | -f FILE... } [FILE]...",  # BusyBox v1.35.0
    ],
    "gunzip": [
        "gunzip [-cft] [FILE]...",  # noqa: B950
        "gunzip [-cfkt] [FILE]...",  # BusyBox 1.30.1
    ],
    "gzip": [
        "gzip [-cfd] [FILE]...",  # noqa: B950
        "gzip [-cfkdt] [FILE]...",  # BusyBox 1.30.1
    ],
    "head": ["head [OPTIONS] [FILE]..."],
    "hexdump": [
        "hexdump [-bcCdefnosvx] [FILE]...",  # noqa: B950
        "hexdump [options] <file>...",  # BusyBox 1.30.1
        "hexdump [-bcdoxCv] [-e FMT] [-f FMT_FILE] [-n LEN] [-s OFS] [FILE]...",  # BusyBox v1.35.0
    ],
    "hostname": [
        "hostname [OPTIONS] [HOSTNAME | -F FILE]",
        "hostname [-sidf] [HOSTNAME | -F FILE]",  # BusyBox v1.35.0
    ],
    "id": [
        "id [OPTIONS] [USER]",
        "id [-ugGnr] [USER]",  # BusyBox v1.35.0
    ],
    "kill": [
        "kill [-s sigspec | -n signum | -sigspec] pid | jobspec ... or kill -l [sigspec]"  # noqa: B950
    ],
    "killall": [
        "killall [-l] [-q] [-SIG] PROCESS_NAME...",
        "killall [-lq] [-SIG] PROCESS_NAME...",  # BusyBox v1.35.0
    ],
    "less": [
        "less [-EMmNh~] [FILE]...",  # noqa: B950
        "less [-EIMmNh~] [FILE]...",  # BusyBox 1.30.1
        "less [-EFIMmNRh~] [FILE]...",  # BusyBox 1.31.0
        "less [-EFIMmNSRh~] [FILE]...",  # BusyBox 1.38.0, FEATURE_LESS_TRUNCATE
    ],
    "ln": [
        "ln [OPTIONS] TARGET... LINK|DIR",
        "ln [-sfnbtv] [-S SUF] TARGET... LINK|DIR",  # BusyBox v1.35.0
    ],
    "logger": [
        "logger [OPTIONS] [MESSAGE]",  # noqa: B950
        "logger [options] [<message>]",  # BusyBox 1.30.1
        "logger [-s] [-t TAG] [-p PRIO] [MESSAGE]",  # BusyBox v1.35.0
    ],
    "logname": ["logname"],
    "ls": [
        "ls [-1AaCxdLHRFplinsehrSXvctu] [-w WIDTH] [FILE]...",  # noqa: B950
        "ls [-1AaCxdLHRFplinshrSXvctu] [-w WIDTH] [FILE]...",  # BusyBox 1.30.1
    ],
    "md5sum": ["md5sum [-c[sw]] [FILE]..."],
    "microcom": [
        "microcom [-d DELAY] [-t TIMEOUT] [-s SPEED] [-X] TTY",
        "microcom [-d DELAY_MS] [-t TIMEOUT_MS ] [-s SPEED] [-X] TTY",  # BusyBox v1.35.0
    ],
    "mkdir": [
        "mkdir [OPTIONS] DIRECTORY...",
        "mkdir [-m MODE] [-p] DIRECTORY...",  # BusyBox v1.35.0
    ],
    "mkfifo": ["mkfifo [-m MODE] NAME"],
    "mknod": [
        "mknod [-m MODE] NAME TYPE MAJOR MINOR",
        "mknod [-m MODE] NAME TYPE [MAJOR MINOR]",  # BusyBox 1.30.1
    ],
    "mktemp": [
        "mktemp [-dt] [-p DIR] [TEMPLATE]",
        "mktemp [-dt] [-p DIR, --tmpdir[=DIR]] [TEMPLATE]",  # BusyBox 1.31.1
    ],
    "more": [
        "more [FILE]...",  # noqa: B950
        "more [options] <file>...",  # BusyBox 1.30.1
    ],
    "mount": [
        "mount [OPTIONS] [-o OPT] DEVICE NODE",
        "mount <operation> <mountpoint> [<target>]",  # BusyBox 1.30.1
    ],
    "mv": [
        "mv [-fin] SOURCE DEST",
        "mv [-finT] SOURCE DEST",  # BusyBox v1.35.0
    ],
    "nc": ["nc [-l] [-p PORT] [IPADDR PORT]"],
    "nohup": ["nohup PROG ARGS"],
    "nslookup": [
        "nslookup [HOST] [SERVER]",
        "nslookup HOST [DNS_SERVER]",  # BusyBox 1.30.1
    ],
    "od": [
        "od [-aBbcDdeFfHhIiLlOovXx] [FILE]",
        "od [-abcdeFfhiloxsv] [FILE]",  # BusyBox 1.37.0
    ],
    "openvt": ["openvt [-c N] [-sw] [PROG ARGS]"],
    "patch": [
        "patch [OPTIONS] [ORIGFILE [PATCHFILE]]",
        "patch [-RNE] [-p N] [-i DIFF] [ORIGFILE [PATCHFILE]]",  # BusyBox v1.35.0
    ],
    "ping": ["ping [OPTIONS] HOST"],
    "ping6": ["ping6 [OPTIONS] HOST"],
    "pkill": [
        "pkill [-l|-SIGNAL] [-fnovx] [-s SID|-P PPID|PATTERN]",
        "pkill [-l|-SIGNAL] [-xfvno] [-s SID|-P PPID|PATTERN]",  # BusyBox v1.35.0
        "pkill [-l|-SIGNAL] [-xfvnoe] [-s SID|-P PPID|PATTERN]",  # BusyBox v1.36.0
    ],
    "printf": ["printf [-v var] format [arguments]"],
    "ps": ["ps"],
    "pwd": ["pwd [-LP]"],
    "readlink": ["readlink [-fnv] FILE"],
    "realpath": ["realpath FILE..."],
    "renice": [
        "renice {{-n INCREMENT} | PRIORITY} [[-p | -g | -u] ID...]",  # noqa: B950
        "renice [-n] <priority> [-p|--pid] <pid>...",  # BusyBox 1.30.1
        "renice [-n] <priority>  -g|--pgrp <pgid>...",  # BusyBox 1.30.1
        "renice [-n] <priority>  -u|--user <user>...",  # BusyBox 1.30.1
        "renice [-n] PRIORITY [[-p | -g | -u] ID...]...",  # BusyBox 1.31.1
        "renice [-n] PRIORITY [[-p|g|u] ID...]...",  # BusyBox v1.35.0
    ],
    "reset": ["reset"],
    "resize": ["resize"],
    "rm": ["rm [-irf] FILE..."],
    "rmdir": [
        "rmdir [OPTIONS] DIRECTORY...",
        "rmdir [-p] DIRECTORY...",  # BusyBox v1.35.0
    ],
    "run-parts": [
        "run-parts [-a ARG]... [-u UMASK] [--reverse] [--test] [--exit-on-error] DIRECTORY"  # noqa: B950
    ],
    "runsv": ["runsv DIR"],
    "runsvdir": ["runsvdir [-P] [-s SCRIPT] DIR"],
    "rx": [
        "rx FILE",
        "rx [options] [filename.if.xmodem]",  # lrzsz 0.12.20
        "rx (lrz) [options] [filename.if.xmodem]",  # lrzsz 0.13.1
    ],
    "sed": [
        "sed [-inrE] [-f FILE]... [-e CMD]... [FILE]...",  # noqa: B950
        "sed [-i[SFX]] [-nrE] [-f FILE]... [-e CMD]... [FILE]...",  # BusyBox 1.30.1
    ],
    "seq": ["seq [-w] [-s SEP] [FIRST [INC]] LAST"],
    "setuidgid": ["setuidgid USER PROG ARGS"],
    "sha1sum": ["sha1sum [-c[sw]] [FILE]..."],
    "sha256sum": ["sha256sum [-c[sw]] [FILE]..."],
    "shuf": [
        "shuf [-e|-i L-H] [-n NUM] [-o FILE] [-z] [FILE|ARG...]",
        "shuf [-n NUM] [-o FILE] [-z] [FILE | -e [ARG...] | -i L-H]",  # BusyBox v1.35.0
    ],
    "sleep": ["sleep [N]..."],
    "softlimit": ["softlimit [-a BYTES] [-m BYTES] [-d BYTES] [-s BYTES] [-l BYTES]"],
    "sort": [
        "sort [-nrugMcszbdfimSTokt] [-o FILE] [-k start[.offset][opts][,end[.offset][opts]] [-t CHAR] [FILE]...",  # noqa: B950
        "sort [-nrugMcszbdfiokt] [-o FILE] [-k start[.offset][opts][,end[.offset][opts]] [-t CHAR] [FILE]...",  # BusyBox 1.30.1  # noqa: B950
        "sort [-nrugMcszbdfiokt] [-o FILE] [-k START[.OFS][OPTS][,END[.OFS][OPTS]] [-t CHAR] [FILE]...",  # BusyBox v1.35.0
        "sort [-nrughMcszbdfiokt] [-o FILE] [-k START[.OFS][OPTS][,END[.OFS][OPTS]] [-t CHAR] [FILE]...",  # BusyBox v1.36.0  # noqa: B950
    ],
    "stat": [
        "stat [OPTIONS] FILE...",
        "stat [-ltf] [-c FMT] FILE...",  # BusyBox v1.35.0
    ],
    "strings": [
        "strings [-afo] [-n LEN] [FILE]...",
        "strings [-fo] [-t o/d/x] [-n LEN] [FILE]...",  # BusyBox 1.30.1
        "strings [-fo] [-t o|d|x] [-n LEN] [FILE]...",  # BusyBox v1.35.0
    ],
    "stty": ["stty [-a|g] [-F DEVICE] [SETTING]..."],
    "sv": ["sv [-v] [-w SEC] CMD SERVICE_DIR..."],
    "sync": ["sync"],
    "tac": ["tac [FILE]..."],
    "tail": ["tail [OPTIONS] [FILE]..."],
    "tar": [
        "tar -[cxtZzJjahmvO] [-X FILE] [-T FILE] [-f TARFILE] [-C DIR] [FILE]...",  # noqa: B950
        "tar c|x|t [-ZzJjahmvokO] [-f TARFILE] [-C DIR] [-T FILE] [-X FILE] [FILE]...",  # BusyBox 1.30.1  # noqa: B950
        "tar c|x|t [-ZzJjahmvokO] [-f TARFILE] [-C DIR] [-T FILE] [-X FILE] [LONGOPT]... [FILE]...",  # BusyBox v1.35.0
    ],
    "tee": ["tee [-ai] [FILE]..."],
    "telnet": ["telnet [-a] [-l USER] HOST [PORT]"],
    "tftp": ["tftp [OPTIONS] HOST [PORT]"],
    "time": [
        "time [-v] PROG ARGS",
        "time [-vpa] [-o FILE] PROG ARGS",  # BusyBox 1.30.1
    ],
    "timeout": [
        "timeout [-t SECS] [-s SIG] PROG ARGS",  # noqa: B950
        "timeout [-s SIG] SECS PROG ARGS",  # BusyBox 1.30.1
        "timeout [-s SIG] [-k KILL_SECS] SECS PROG ARGS",  # BusyBox v1.35.0
    ],
    "top": [
        "top [-b] [-nCOUNT] [-dSECONDS]",
        "top [-b] [-n COUNT] [-d SECONDS]",  # BusyBox 1.31.0
        "top [-bm] [-n COUNT] [-d SECONDS]",  # BusyBox 1.38.0, FEATURE_TOPMEM
    ],
    "touch": [
        "touch [-c] [-d DATE] [-t DATE] [-r FILE] FILE...",
        "touch [-cham] [-d DATE] [-t DATE] [-r FILE] FILE...",  # BusyBox v1.35.0
    ],
    "tr": ["tr [-cds] STRING1 [STRING2]"],
    "traceroute": [
        "traceroute [-46FIlnrv] [-f 1ST_TTL] [-m MAXTTL] [-q PROBES] [-p PORT]",
        "traceroute [-46Flnrv] [-f 1ST_TTL] [-m MAXTTL] [-q PROBES] [-p PORT]",  # BusyBox v1.35.0
    ],
    "traceroute6": [
        "traceroute6 [-nrv] [-m MAXTTL] [-q PROBES] [-p PORT]",
        "traceroute6 [-nrv] [-f 1ST_TTL] [-m MAXTTL] [-q PROBES] [-p PORT]",  # BusyBox v1.35.0
    ],
    "tty": ["tty"],
    "umount": [
        "umount [OPTIONS] FILESYSTEM|DIRECTORY",  # noqa: B950
        "umount [options] <source> | <directory>",  # BusyBox 1.30.1
        "umount [-rlfda] [-t FSTYPE] FILESYSTEM|DIRECTORY",  # BusyBox v1.35.0
    ],
    "uname": ["uname [-amnrspvio]"],
    "uniq": [
        "uniq [-cdu][-f,s,w N] [INPUT [OUTPUT]]",
        "uniq [-cduiz] [-f,s,w N] [FILE [OUTFILE]]",  # BusyBox v1.35.0
    ],
    "unlink": ["unlink FILE"],
    "unzip": [
        "unzip [-lnopq] FILE[.zip] [FILE]... [-x FILE...] [-d DIR]",  # noqa: B950
        "unzip [-lnojpq] FILE[.zip] [FILE]... [-x FILE...] [-d DIR]",  # BusyBox 1.30.1
        "unzip [-lnojpq] FILE[.zip] [FILE]... [-x FILE]... [-d DIR]",  # BusyBox v1.35.0
        "unzip [-lnojpqK] FILE[.zip] [FILE]... [-x FILE]... [-d DIR]",  # BusyBox v1.36.1
    ],
    "uptime": ["uptime"],
    "users": ["users"],
    "usleep": ["usleep N"],
    "vi": [
        "vi [OPTIONS] [FILE]...",
        "vi [-c CMD] [-H] [FILE]...",  # BusyBox v1.35.0
    ],
    "vlock": ["vlock [-a]"],
    "watch": [
        "watch [-n SEC] [-t] PROG ARGS",
        "watch [-n SEC] [-tx] PROG ARGS",  # BusyBox v1.38.0
    ],
    "wc": [
        "wc [-clwL] [FILE]...",
        "wc [-cmlwL] [FILE]...",  # BusyBox 1.31.0
    ],
    "wget": [
        "wget [-c|--continue] [-s|--spider] [-q|--quiet] [-O|--output-document FILE]",  # noqa: B950
        "wget [-c|--continue] [--spider] [-q|--quiet] [-O|--output-document FILE]",  # BusyBox 1.30.1  # noqa: B950
        "wget [-cqS] [--spider] [-O FILE] [-o LOGFILE] [--header STR]",  # BusyBox v1.35.0
    ],
    "which": [
        "which [COMMAND]...",
        "which [-a] COMMAND...",  # BusyBox v1.35.0
    ],
    "who": [
        "who [-a]",
        "who [-aH]",  # BusyBox v1.35.0
    ],
    "whoami": ["whoami"],
    "xargs": ["xargs [OPTIONS] [PROG ARGS]"],
    "yes": ["yes [STRING]"],
    "zcat": ["zcat [FILE]..."],
}
