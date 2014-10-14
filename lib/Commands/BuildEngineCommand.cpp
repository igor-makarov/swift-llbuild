//===-- BuildEngineCommand.cpp --------------------------------------------===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2015 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://swift.org/LICENSE.txt for license information
// See http://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//

#include "llbuild/Commands/Commands.h"

#include "llbuild/Core/BuildEngine.h"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>

using namespace llbuild;

#pragma mark - Ackermann Build Command

namespace {

#ifndef NDEBUG
static uint64_t ack(int m, int n) {
  // Memoize using an array of growable vectors.
  std::vector<std::vector<int>> MemoTable(m+1);

  std::function<int(int,int)> ack_internal;
  ack_internal = [&] (int m, int n) {
    assert(m >= 0 && n >= 0);
    auto& MemoRow = MemoTable[m];
    if (n >= MemoRow.size())
        MemoRow.resize(n + 1);
    if (MemoRow[n] != 0)
      return MemoRow[n];

    int Result;
    if (m == 0) {
      Result = n + 1;
    } else if (n == 0) {
      Result = ack_internal(m - 1, 1);
    } else {
      Result = ack_internal(m - 1, ack_internal(m, n - 1));
    };

    MemoRow[n] = Result;
    return Result;
  };

  return ack_internal(m, n);
}
#endif

static core::Task* BuildAck(core::BuildEngine& engine, int M, int N) {
  struct AckermannTask : core::Task {
    int M, N;
    int RecursiveResultA = 0;
    int RecursiveResultB = 0;

    AckermannTask(int M, int N) : Task("ack()"), M(M), N(N) { }

    virtual void provideValue(core::BuildEngine& engine, uintptr_t InputID,
                              core::ValueType Value) override {
      if (InputID == 0) {
        RecursiveResultA = Value;

        // Request the second recursive result, if needed.
        if (M != 0 && N != 0) {
          char InputKey[32];
          sprintf(InputKey, "ack(%d,%d)", M - 1, RecursiveResultA);
          engine.taskNeedsInput(this, InputKey, 1);
        }
      } else {
        assert(InputID == 1 && "invalid input ID");
        RecursiveResultB = Value;
      }
    }

    virtual void start(core::BuildEngine& engine) override {
      // Request the first recursive result, if necessary.
      if (M == 0) {
        ;
      } else if (N == 0) {
        char InputKey[32];
        sprintf(InputKey, "ack(%d,%d)", M-1, 1);
        engine.taskNeedsInput(this, InputKey, 0);
      } else {
        char InputKey[32];
        sprintf(InputKey, "ack(%d,%d)", M, N-1);
        engine.taskNeedsInput(this, InputKey, 0);
      }
    }

    virtual core::ValueType finish() override {
      if (M == 0)
        return N + 1;
      assert(RecursiveResultA != 0);
      if (N == 0)
        return RecursiveResultA;
      assert(RecursiveResultB != 0);
      return RecursiveResultB;
    }
  };

  // Create the task.
  return engine.registerTask(new AckermannTask(M, N));
}

static void RunAckermannBuild(int M, int N) {
  // Compute the value of ackermann(M, N) using the build system.
  assert(M >= 0 && M < 4);
  assert(N >= 0);

  // First, create rules for each of the necessary results.
  core::BuildEngine engine;
  int NumRules = 0;
  for (int i = 0; i <= M; ++i) {
    int UpperBound;
    if (i == 0 || i == 1) {
      UpperBound = int(pow(2, N+3) - 3) + 1;
    } else if (i == 2) {
      UpperBound = int(pow(2, N+3 - 1) - 3) + 1;
    } else {
      assert(i == M);
      UpperBound = N + 1;
    }
    for (int j = 0; j <= UpperBound; ++j) {
      char Name[32];
      sprintf(Name, "ack(%d,%d)", i, j);
      engine.addRule({ Name, [i, j] (core::BuildEngine& engine) {
            return BuildAck(engine, i, j); } });
      ++NumRules;
    }
  }

  char Key[32];
  sprintf(Key, "ack(%d,%d)", M, N);
  core::ValueType Result = engine.build(Key);
  std::cout << "ack(" << M << ", " << N << ") = " << Result << "\n";
  if (N < 10) {
#ifndef NDEBUG
    core::ValueType Expected = ack(M, N);
    assert(Result == Expected);
#endif
  }
  std::cout << "... computed using " << NumRules << " rules\n";
}

static int ExecuteAckermannCommand(const std::vector<std::string> &Args) {
  if (Args.size() != 2) {
    fprintf(stderr, "error: %s: invalid number of arguments\n", getprogname());
    return 1;
  }

  const char *Str = Args[0].c_str();
  int M = ::strtol(Str, (char**)&Str, 10);
  if (*Str != '\0') {
    fprintf(stderr, "error: %s: invalid argument '%s' (expected integer)\n",
            getprogname(), Args[0].c_str());
    return 1;
  }
  Str = Args[1].c_str();
  int N = ::strtol(Str, (char**)&Str, 10);
  if (*Str != '\0') {
    fprintf(stderr, "error: %s: invalid argument '%s' (expected integer)\n",
            getprogname(), Args[1].c_str());
    return 1;
  }

  if (M >= 4) {
    fprintf(stderr, "error: %s: invalid argument M = '%d' (too large)\n",
            getprogname(), M);
    return 1;
  }

  if (N >= 1024) {
    fprintf(stderr, "error: %s: invalid argument N = '%d' (too large)\n",
            getprogname(), N);
    return 1;
  }
    
  RunAckermannBuild(M, N);
  return 0;
}

}

#pragma mark - Build Engine Top-Level Command

static void usage() {
  fprintf(stderr, "Usage: %s buildengine [--help] <command> [<args>]\n",
          getprogname());
  fprintf(stderr, "\n");
  fprintf(stderr, "Available commands:\n");
  fprintf(stderr, "  ack           -- Compute Ackermann\n");
  fprintf(stderr, "\n");
  exit(1);
}

int commands::ExecuteBuildEngineCommand(const std::vector<std::string> &Args) {
  // Expect the first argument to be the name of another subtool to delegate to.
  if (Args.empty() || Args[0] == "--help")
    usage();

  if (Args[0] == "ack") {
    return ExecuteAckermannCommand({Args.begin()+1, Args.end()});
  } else {
    fprintf(stderr, "error: %s: unknown command '%s'\n", getprogname(),
            Args[0].c_str());
    return 1;
  }
}