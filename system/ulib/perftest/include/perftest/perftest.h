// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <fbl/function.h>
#include <perftest/results.h>

// This is a library for writing performance tests.  It supports
// performance tests that involve running an operation repeatedly,
// sequentially, and recording the times taken by each run of the
// operation.  (It does not yet support other types of performance test,
// such as where we run an operation concurrently in multiple threads.)
//
// There are two ways to implement a test:
//
// 1) For tests that don't need to reuse any test fixtures across each run,
// use RegisterSimpleTest():
//
//   bool TestFooOp() {
//       FooOp();  // The operation that we are timing.
//       return true;  // Indicate success.
//   }
//   void RegisterTests() {
//       perftest::RegisterSimpleTest<TestFooOp>("FooOp");
//   }
//   PERFTEST_CTOR(RegisterTests);
//
// 2) For tests that do need to reuse test fixtures across each run, use
// the more general RegisterTest():
//
//   bool TestFooObjectOp(perftest::RepeatState* state) {
//       FooObject obj;  // Fixture that is reused across test runs.
//       while (state->KeepRunning()) {
//           obj.FooOp();  // The operation that we are timing.
//       }
//       return true;  // Indicate success.
//   }
//   void RegisterTests() {
//       perftest::RegisterTest("FooObjectOp", TestFooObjectOp);
//   }
//   PERFTEST_CTOR(RegisterTests);
//
// Test registration is done using function calls in order to make it easy
// to instantiate parameterized tests multiple times.
//
// Background: The "KeepRunning()" interface is based on the interface used
// by the gbenchmark library (https://github.com/google/benchmark).
//
//
// ## Multi-step tests
//
// Sometimes we have a performance test which consists of multiple steps
// that depend on each other, and we want to measure the times taken by
// each step.  The perftest library allows doing this.
//
// For example, if we're interested in the performance of mutexes, we might
// want to measure the times taken by mtx_lock() and by mtx_unlock().  We
// can't just call mtx_lock() on its own in a loop or call mtx_unlock() on
// its own in a loop -- the mutex interface requires that the two calls are
// paired.  Nevertheless, we want to measure the times for each of
// mtx_lock() and mtx_unlock() in case one is slower than the other or
// exhibits more variation in timing.  This test can be written as follows:
//
//   // Test locking and unlocking a mutex in the uncontended case.
//   bool MutexUncontendedTest(perftest::RepeatState* state) {
//       state->DeclareStep("lock");  // Declares step 1.
//       state->DeclareStep("unlock");  // Declares step 2.
//       mtx_t mutex = MTX_INIT;
//       while (state->KeepRunning()) {
//           // Each iteration of this loop is a "test run".
//           mtx_lock(&mutex);  // Step 1: this operation is timed.
//           state->NextStep();
//           mtx_unlock(&mutex);  // Step 2: this operation is timed.
//       }
//       return true;
//   }
//
// For a multi-step test, the test function should call
// state->DeclareStep() once for each step to declare the step names,
// before its first call to KeepRunning().  Then it should call
// state->NextStep() between each step.

namespace perftest {

// This object is passed to the test function.  It controls the iteration
// of test runs and records the times taken by test runs.
//
// This is a pure virtual interface so that one can potentially use a test
// runner other than the one provided by this library.
class RepeatState {
public:
    // KeepRunning() should be called by test functions using a "while"
    // loop shown above.  A call to KeepRunning() indicates the start or
    // end of a test run, or both.  KeepRunning() returns a bool indicating
    // whether the caller should do another test run.
    virtual bool KeepRunning() = 0;

    // Calls to DeclareStep() specify the names of the steps that a test
    // consists of.  This is used for multi-step tests.  If DeclareStep()
    // is not called, the test will just have a single step.  DeclareStep()
    // should not be called after the first call to KeepRunning().
    virtual void DeclareStep(const char* name) = 0;

    // In multi-step tests, NextStep() should be called between each step
    // within a test run.  So if a test has N steps, NextStep() should be
    // called N-1 times between calls to KeepRunning().
    virtual void NextStep() = 0;
};

typedef bool TestFunc(RepeatState* state);
typedef bool SimpleTestFunc();

void RegisterTest(const char* name, fbl::Function<TestFunc> test_func);

// Convenience routine for registering parameterized perf tests.
template <typename Func, typename Arg, typename... Args>
void RegisterTest(const char* name, Func test_func, Arg arg, Args... args) {
    auto wrapper_func = [=](RepeatState* state) {
        return test_func(state, arg, args...);
    };
    RegisterTest(name, wrapper_func);
}

// Convenience routine for registering a perf test that is specified by a
// function.  This is for tests that don't set up any fixtures that are
// shared across invocations of the function.
//
// This takes the function as a template parameter rather than as a value
// parameter in order to avoid the potential cost of an indirect function
// call.
template <SimpleTestFunc test_func>
void RegisterSimpleTest(const char* test_name) {
    auto wrapper_func = [](RepeatState* state) {
        while (state->KeepRunning()) {
            if (!test_func()) {
                return false;
            }
        }
        return true;
    };
    RegisterTest(test_name, fbl::move(wrapper_func));
}

// Entry point for the perf test runner that a test executable should call
// from main().  This will run the registered perf tests and/or unit tests,
// based on the command line arguments.  (See the "--help" output for more
// details.)
int PerfTestMain(int argc, char** argv);

}  // namespace perftest

// This calls func() at startup time as a global constructor.  This is
// useful for registering perf tests.  This is similar to declaring func()
// with __attribute__((constructor)), but portable.
#define PERFTEST_CTOR(func) \
    namespace { \
    struct FuncCaller_##func { \
        FuncCaller_##func() { func(); } \
    } global; \
    }
