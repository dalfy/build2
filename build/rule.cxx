// file      : build/rule.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2015 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build/rule>

#include <utility>      // move()
#include <system_error>

#include <butl/filesystem>

#include <build/scope>
#include <build/target>
#include <build/algorithm>
#include <build/diagnostics>
#include <build/context>

using namespace std;
using namespace butl;

namespace build
{
  // file_rule
  //
  // Note that this rule is special. It is the last, fallback rule. If
  // it doesn't match, then no other rule can possibly match and we have
  // an error. It also cannot be ambigious with any other rule. As a
  // result the below implementation bends or ignores quite a few rules
  // that normal implementations should follow. So you probably shouldn't
  // use it as a guide to implement your own, normal, rules.
  //
  match_result file_rule::
  match (action a, target& t, const string&) const
  {
    tracer trace ("file_rule::match");

    // While strictly speaking we should check for the file's existence
    // for every action (because that's the condition for us matching),
    // for some actions this is clearly a waste. Say, perform_clean: we
    // are not doing anything for this action so not checking if the file
    // exists seems harmless. So the overall guideline seems to be this:
    // if we don't do anything for the action (other than performing it
    // on the prerequisites), then we match.
    //
    switch (a)
    {
    case perform_update_id:
      {
        path_target& pt (dynamic_cast<path_target&> (t));

        // Assign the path. While normally we shouldn't do this in match(),
        // no other rule should ever be ambiguous with the fallback one.
        //
        if (pt.path ().empty ())
          pt.derive_path ();

        // We cannot just call pt.mtime() since we haven't matched yet.
        //
        timestamp ts (file_mtime (pt.path ()));
        pt.mtime (ts);

        if (ts != timestamp_nonexistent)
          return t;

        level3 ([&]{trace << "no existing file for target " << t;});
        return nullptr;
      }
    default:
      return t;
    }
  }

  recipe file_rule::
  apply (action a, target& t, const match_result&) const
  {
    // Update triggers the update of this target's prerequisites
    // so it would seem natural that we should also trigger their
    // cleanup. However, this possibility is rather theoretical
    // since such an update would render this target out of date
    // which in turn would lead to an error. So until we see a
    // real use-case for this functionality, we simply ignore
    // the clean operation.
    //
    if (a.operation () == clean_id)
      return noop_recipe;

    // If we have no prerequisites, then this means this file
    // is up to date. Return noop_recipe which will also cause
    // the target's state to be set to unchanged. This is an
    // important optimization on which quite a few places that
    // deal with predominantly static content rely.
    //
    if (!t.has_prerequisites ())
      return noop_recipe;

    // Search and match all the prerequisites.
    //
    search_and_match_prerequisites (a, t);
    return a == perform_update_id ? &perform_update : default_recipe;
  }

  target_state file_rule::
  perform_update (action a, target& t)
  {
    // Make sure the target is not older than any of its prerequisites.
    //
    timestamp mt (dynamic_cast<path_target&> (t).mtime ());

    for (target* pt: t.prerequisite_targets)
    {
      target_state ts (execute (a, *pt));

      // If this is an mtime-based target, then compare timestamps.
      //
      if (auto mpt = dynamic_cast<const mtime_target*> (pt))
      {
        timestamp mp (mpt->mtime ());

        if (mt < mp)
          fail << "no recipe to " << diag_do (a, t) <<
            info << "prerequisite " << *pt << " is ahead of " << t
               << " by " << (mp - mt);
      }
      else
      {
        // Otherwise we assume the prerequisite is newer if it was changed.
        //
        if (ts == target_state::changed)
          fail << "no recipe to " << diag_do (a, t) <<
            info << "prerequisite " << *pt << " is ahead of " << t
               << " because it was updated";
      }
    }

    return target_state::unchanged;
  }

  file_rule file_rule::instance;

  // alias_rule
  //
  match_result alias_rule::
  match (action, target& t, const string&) const
  {
    return t;
  }

  recipe alias_rule::
  apply (action a, target& t, const match_result&) const
  {
    search_and_match_prerequisites (a, t);
    return default_recipe;
  }

  alias_rule alias_rule::instance;

  // fsdir_rule
  //
  match_result fsdir_rule::
  match (action, target& t, const string&) const
  {
    return t;
  }

  recipe fsdir_rule::
  apply (action a, target& t, const match_result&) const
  {
    // Inject dependency on the parent directory. Note that we
    // don't do it for clean since we shouldn't be removing it.
    //
    if (a.operation () != clean_id)
      inject_parent_fsdir (a, t);

    search_and_match_prerequisites (a, t);

    switch (a)
    {
    case perform_update_id: return &perform_update;
    case perform_clean_id: return &perform_clean;
    default: assert (false); return default_recipe;
    }
  }

  target_state fsdir_rule::
  perform_update (action a, target& t)
  {
    target_state ts (target_state::unchanged);

    // First update prerequisites (e.g. create parent directories)
    // then create this directory.
    //
    if (!t.prerequisite_targets.empty ())
      ts = execute_prerequisites (a, t);

    const dir_path& d (t.dir); // Everything is in t.dir.

    // Generally, it is probably correct to assume that in the majority
    // of cases the directory will already exist. If so, then we are
    // going to get better performance by first checking if it indeed
    // exists. See try_mkdir() for details.
    //
    if (!dir_exists (d))
    {
      if (verb)
        text << "mkdir " << d;
      else
        text << "mkdir " << t;

      try
      {
        try_mkdir (d);
      }
      catch (const system_error& e)
      {
        fail << "unable to create directory " << d << ": " << e.what ();
      }

      ts |= target_state::changed;
    }

    return ts;
  }

  target_state fsdir_rule::
  perform_clean (action a, target& t)
  {
    // The reverse order of update: first delete this directory,
    // then clean prerequisites (e.g., delete parent directories).
    //
    // Don't fail if we couldn't remove the directory because it
    // is not empty (or is current working directory). In this
    // case rmdir() will issue a warning when appropriate.
    //
    target_state ts (rmdir (t.dir, t)
                     ? target_state::changed
                     : target_state::unchanged);

    if (!t.prerequisite_targets.empty ())
      ts |= reverse_execute_prerequisites (a, t);

    return ts;
  }

  fsdir_rule fsdir_rule::instance;

  // fallback_rule
  //
  fallback_rule fallback_rule::instance;
}
