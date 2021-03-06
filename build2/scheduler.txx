// file      : build2/scheduler.txx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <cerrno>

namespace build2
{
  template <typename F, typename... A>
  bool scheduler::
  async (size_t start_count, atomic_count& task_count, F&& f, A&&... a)
  {
    using task = task_type<F, A...>;

    static_assert (sizeof (task) <= sizeof (task_data::data),
                   "insufficient space");

    static_assert (std::is_trivially_destructible<task>::value,
                   "not trivially destructible");

    // If running serially, then run the task synchronously. In this case
    // there is no need to mess with task count.
    //
    if (max_active_ == 1)
    {
      forward<F> (f) (forward<A> (a)...);

      // See if we need to call the monitor (see the concurrent version in
      // execute() for details).
      //
      if (monitor_count_ != nullptr)
      {
        size_t v (monitor_count_->load (memory_order_relaxed));
        if (v != monitor_init_)
        {
          size_t t (monitor_tshold_.load (memory_order_relaxed));
          if (v > monitor_init_ ? (v >= t) : (v <= t))
            monitor_tshold_.store (monitor_func_ (v), memory_order_relaxed);
        }
      }

      return false;
    }

    // Try to push the task into the queue falling back to running serially
    // if the queue is full.
    //
    task_queue* tq (task_queue_); // Single load.
    if (tq == nullptr)
      tq = &create_queue ();

    {
      lock ql (tq->mutex);

      if (tq->shutdown)
        throw_generic_error (ECANCELED);

      if (task_data* td = push (*tq))
      {
        // Package the task (under lock).
        //
        new (&td->data) task {
          &task_count,
          start_count,
          decay_copy (forward<F> (f)),
          typename task::args_type (decay_copy (forward<A> (a))...)};

        td->thunk = &task_thunk<F, A...>;

        // Increment the task count. This has to be done under lock to prevent
        // the task from decrementing the count before we had a chance to
        // increment it.
        //
        task_count.fetch_add (1, std::memory_order_release);
      }
      else
      {
        tq->stat_full++;

        // We have to perform the same mark adjust/restore as in pop_back()
        // since the task we are about to execute synchronously may try to
        // work the queue.
        //
        // It would have been cleaner to package all this logic into push()
        // but that would require dragging function/argument types into it.
        //
        size_t& s (tq->size);
        size_t& t (tq->tail);
        size_t& m (tq->mark);

        size_t om (m);
        m = task_queue_depth_;

        ql.unlock ();
        forward<F> (f) (forward<A> (a)...); // Should not throw.

        if (om != task_queue_depth_)
        {
          ql.lock ();
          m = s == 0 ? t : om;
        }

        return false;
      }
    }

    // If there is a spare active thread, wake up (or create) the helper
    // (unless someone already snatched the task).
    //
    if (queued_task_count_.load (std::memory_order_consume) != 0)
    {
      lock l (mutex_);

      if (active_ < max_active_)
        activate_helper (l);
    }

    return true;
  }

  template <typename F, typename... A>
  void scheduler::
  task_thunk (scheduler& s, lock& ql, void* td)
  {
    using task = task_type<F, A...>;

    // Move the data and release the lock.
    //
    task t (move (*static_cast<task*> (td)));
    ql.unlock ();

    t.thunk (std::index_sequence_for<A...> ());

    atomic_count& tc (*t.task_count);
    if (tc.fetch_sub (1, memory_order_release) - 1 <= t.start_count)
      s.resume (tc); // Resume waiters, if any.
  }
}
