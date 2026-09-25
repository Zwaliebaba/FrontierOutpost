#include "Thread.h"

#include "Job.h"
#include "Lock.h"

#include <atomic>
#include <chrono>
#include <climits>
#include <memory>
#include <thread>

namespace {
  Lock GetThreadLock() {
    /* NOTE : Locks cannot be static destructed. */
    static Lock* lock = new Lock(Lock_Create());
    return *lock;
  }

  struct ThreadImpl : public ThreadT {
    Job job;
    /* Shared with the thread, which outlives this object once it is detached. */
    std::shared_ptr<std::atomic<bool>> finished;
    std::jthread thread;

    ThreadImpl(Job const& job) :
      job(job),
      finished(std::make_shared<std::atomic<bool> >(false))
    {
      ScopedLock lock(GetThreadLock());
      job->OnBegin();
      /* The thread gets the job as a raw pointer: reference counts are not atomic, so only this
         thread touches them. */
      JobT* run = job.t;
      thread = std::jthread([run, done = finished] {
        run->OnRun(UINT_MAX);
        done->store(true, std::memory_order_release);
      });
    }

    ~ThreadImpl() {
      if (!IsFinished()) {
        /* Still running: no OnEnd, which would race with it. */
        Terminate();
        return;
      }
      if (thread.joinable())
        thread.join();
      job->OnEnd();
    }

    Job GetJob() const {
      return job;
    }

    bool IsFinished() const {
      return finished->load(std::memory_order_acquire);
    }

    /* SFML ended the thread with TerminateThread, which has no safe equivalent. The thread is
       detached instead, and runs until its job returns or the process exits. The extra reference
       is never released, so the job outlives it. */
    void Terminate() {
      ScopedLock lock(GetThreadLock());
      if (thread.joinable()) {
        job->RefCountIncrement();
        thread.detach();
      }
    }

    void Wait() {
      if (thread.joinable())
        thread.join();
    }
  };
}

Thread Thread_Create(Job const& job) {
  return new ThreadImpl(job);
}

DefineFunction(Thread_SleepMS) {
  std::this_thread::sleep_for(std::chrono::milliseconds(args.ms));
}

DefineFunction(Thread_SleepUS) {
  std::this_thread::sleep_for(std::chrono::microseconds(args.us));
}
