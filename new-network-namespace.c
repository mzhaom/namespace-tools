#define _GNU_SOURCE

// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <linux/capability.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int global_debug = 0;

#define PRINT_DEBUG(...) do { if (global_debug) {fprintf(stderr, "sandbox.c: " __VA_ARGS__);}} while(0)

#define CHECK_CALL(x) if ((x) == -1) { perror(#x); exit(1); }
#define CHECK_NOT_NULL(x) if (x == NULL) { perror(#x); exit(1); }
#define DIE() do { fprintf(stderr, "Error in %d\n", __LINE__); exit(-1); } while(0);

//
// Options parsing result
//
struct Options {
  char **args;          // Command to run (-C / --)
};

// Print out a usage error. argc and argv are the argument counter
// and vector, fmt is a format string for the error message to print.
void Usage(int argc, char **argv, char *fmt, ...);
// Parse the command line flags and return the result in an
// Options structure passed as argument.
void ParseCommandLine(int argc, char **argv, struct Options *opt);

void SetupUserNamespace(int uid, int gid);
// Write the file "filename" using a format string specified by "fmt".
// Returns -1 on failure.
int WriteFile(const char *filename, const char *fmt, ...);

// Run the command specified by the argv array and kill it after
// timeout seconds.
void SpawnCommand(char **argv);

int main(int argc, char *argv[]) {
  struct Options opt = {
    .args = NULL,
  };
  ParseCommandLine(argc, argv, &opt);
  int uid = getuid();
  int gid = getgid();

  // create new namespaces in which this process and its children will live
  CHECK_CALL(unshare(CLONE_NEWNS | CLONE_NEWUSER | CLONE_NEWNET));
  // CHECK_CALL(mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL));
  // Create the sandbox directory layout
  // SetupDirectories(&opt);
  // Set the user namespace (user_namespaces(7))
  SetupUserNamespace(uid, gid);

  // Finally call the command
  SpawnCommand(opt.args);
  return 0;
}

void SpawnCommand(char **argv) {
  for (int i = 0; argv[i] != NULL; i++) {
    PRINT_DEBUG("arg: %s\n", argv[i]);
  }

  // if the execvp below fails with "No such file or directory" it means that:
  // a) the binary is not in the sandbox (which means it wasn't included in
  // the inputs)
  // b) the binary uses shared library which is not inside sandbox - you can
  // check for that by running "ldd ./a.out" (by default directories
  // starting with /lib* and /usr/lib* should be there)
  // c) the binary uses elf interpreter which is not inside sandbox - you can
  // check for that by running "readelf -a a.out | grep interpreter" (the
  // sandbox code assumes that it is either in /lib*/ or /usr/lib*/)
  CHECK_CALL(execvp(argv[0], argv));
  PRINT_DEBUG("Exec failed near %s:%d\n", __FILE__, __LINE__);
  exit(1);
}

int WriteFile(const char *filename, const char *fmt, ...) {
  int r;
  va_list ap;
  FILE *stream = fopen(filename, "w");
  if (stream == NULL) {
    return -1;
  }
  va_start(ap, fmt);
  r = vfprintf(stream, fmt, ap);
  va_end(ap);
  if (r >= 0) {
    r = fclose(stream);
  }
  return r;
}

void SetupUserNamespace(int uid, int gid) {
  // Disable needs for CAP_SETGID
  int r = WriteFile("/proc/self/setgroups", "deny");
  if (r < 0 && errno != ENOENT) {
    // Writing to /proc/self/setgroups might fail on earlier
    // version of linux because setgroups does not exist, ignore.
    perror("WriteFile(\"/proc/self/setgroups\", \"deny\")");
    exit(-1);
  }
  // set group and user mapping from outer namespace to inner:
  // no changes in the parent, be root in the child
  CHECK_CALL(WriteFile("/proc/self/uid_map", "0 %d 1\n", uid));
  CHECK_CALL(WriteFile("/proc/self/gid_map", "0 %d 1\n", gid));

  CHECK_CALL(setresuid(0, 0, 0));
  CHECK_CALL(setresgid(0, 0, 0));
}

//
// Command line parsing
//
void Usage(int argc, char **argv, char *fmt, ...) {
  int i;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);

  fprintf(stderr,
          "\nCreate a new empty network namespace(plus user namespace) for testing"
          "\nUsage: %s [-C|--] command arg1\n",
          argv[0]);
  fprintf(stderr, "  provided:");
  for (i = 0; i < argc; i++) {
    fprintf(stderr, " %s", argv[i]);
  }
  fprintf(stderr,
          "\nMandatory arguments:\n"
          "  [--] command to run inside sandbox, followed by arguments\n"
          "\n"
          "Optional arguments:\n"
          "  -D if set, debug info will be printed\n");
  exit(1);
}

void ParseCommandLine(int argc, char **argv, struct Options *opt) {
  extern char *optarg;
  extern int optind, optopt;
  int c;

  while ((c = getopt(argc, argv, "+:D")) != -1) {
    switch(c) {
      case 'D':
        global_debug = 1;
        break;
      case '?':
        Usage(argc, argv, "Unrecognized argument: -%c (%d)", optopt, optind);
        break;
      case ':':
        Usage(argc, argv, "Flag -%c requires an argument", optopt);
        break;
    }
  }

  opt->args = argv + optind;
  if (argc <= optind) {
    Usage(argc, argv, "No command specified");
  }
}
