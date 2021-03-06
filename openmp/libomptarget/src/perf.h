#ifndef _OMPTARGET_PERF_H_
#define _OMPTARGET_PERF_H_
#include <string>
#include <omptarget.h>
#include <chrono>
#include <iostream>
#include <iomanip>

#include "device.h"

# define _PERF

#ifdef _PERF
#define PERF_WRAP(...) if (Perf.Enabled) do { __VA_ARGS__;} while(0);
#else
#define PERF_WRAP(...)
#endif

using namespace std;
using namespace std::chrono;

struct PerfBaseTy {
  string Name;
  int func();
  virtual void dump() {};
  struct PerfBaseTy *setName(string str) {
    Name = str;
    return this;
  };
};

struct PerfEventTy : public PerfBaseTy {
  float Time; // elapsed time in sec
  int Count;
  int StartCnt;
  bool Lock;
  PerfEventTy *LockTarget, *LockAction;

  high_resolution_clock::time_point StartTime;
  duration<double> time_span;

  PerfEventTy(): Time(0), Count(0), StartCnt(0),
      Lock(false), LockTarget(NULL) {};
  void setLockTarget(PerfEventTy *target) {
    LockTarget = target;
  };
  void setLockAction(PerfEventTy *action_target) {
    LockAction = action_target;
  }
  void start();
  void end();
  void dump();
};

// Try to get bulk alloc size
struct PerfCountTy : public PerfBaseTy {
  unsigned long Sum;
  int Count;
  void add(unsigned long count) {
    Sum += count;
    Count++;
  }
  void dump() {
    fprintf(stderr, "%-11s , %7d , %10lu\n", Name.c_str(), Count, Sum);
  }
  PerfCountTy(): Count(0), Sum(0) {}
};

struct BulkMemCount : public PerfCountTy {
  using PerfCountTy::PerfCountTy;
  void get(int64_t device_id);
};

struct PerfRecordTy {
  bool Enabled;

  PerfEventTy Runtime;
  PerfEventTy Kernel;
  PerfEventTy H2DTransfer;
  PerfEventTy UpdatePtr;
  PerfEventTy D2HTransfer;

  PerfEventTy updateH2D;
  PerfEventTy updateD2H;

  PerfEventTy RTDataBegin;
  PerfEventTy RTDataUpdate;
  PerfEventTy RTDataEnd;
  PerfEventTy RTTarget;

  PerfEventTy Unnamed;

  PerfCountTy Parallelism;
  PerfCountTy ATTableSize;

  BulkMemCount TargetMem;

  std::vector<PerfBaseTy*> Perfs;
  PerfRecordTy() : Enabled(false) {
#define SET_PERF_NAME(Name) Perfs.push_back(Name.setName(#Name));
    SET_PERF_NAME(Runtime); // NOTE this contains following 4
    SET_PERF_NAME(Kernel);
    SET_PERF_NAME(UpdatePtr);
    SET_PERF_NAME(H2DTransfer); //  NOTE this contains UpdatePtr
    SET_PERF_NAME(D2HTransfer);

    SET_PERF_NAME(updateH2D);
    SET_PERF_NAME(updateD2H);

    SET_PERF_NAME(RTTarget);
    SET_PERF_NAME(RTDataBegin);
    SET_PERF_NAME(RTDataUpdate);
    SET_PERF_NAME(RTDataEnd);

//SET_PERF_NAME(Unnamed);

    SET_PERF_NAME(Parallelism);
    SET_PERF_NAME(ATTableSize);
    SET_PERF_NAME(TargetMem);
#undef SET_PERF_NAME
    UpdatePtr.setLockTarget(&H2DTransfer);
    H2DTransfer.setLockAction(&updateH2D);
    D2HTransfer.setLockAction(&updateD2H);


  };
  void dump();
  void init() {Enabled = true;}
  bool isEnabled() {return Enabled;}
};

extern PerfRecordTy Perf;

#endif
