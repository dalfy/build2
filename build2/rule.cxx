// file      : build2/rule.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2019 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/rule.hxx>

#include <build2/scope.hxx>
#include <build2/target.hxx>
#include <build2/context.hxx>
#include <build2/algorithm.hxx>
#include <build2/filesystem.hxx>
#include <build2/diagnostics.hxx>

using namespace std;
using namespace butl;

namespace build2
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
  bool file_rule::
  match (action a, target& t, const string&) const
  {
    tracer trace ("file_rule::match");

    // While strictly speaking we should check for the file's existence
    // for every action (because that's the condition for us matching),
    // for some actions this is clearly a waste. Say, perform_clean: we
    // are not doing anything for this action so not checking if the file
    // exists seems harmless.
    //
    switch (a)
    {
    case perform_clean_id:
      return true;
    default:
      {
        // While normally we shouldn't do any of this in match(), no other
        // rule should ever be ambiguous with the fallback one and path/mtime
        // access is atomic. In other words, we know what we are doing but
        // don't do this in normal rules.

        // First check the timestamp. This takes care of the special "trust
        // me, this file exists" situations (used, for example, for installed
        // stuff where we know it's there, just not exactly where).
        //
        mtime_target& mt (t.as<mtime_target> ());

        timestamp ts (mt.mtime ());

        if (ts != timestamp_unknown && ts != timestamp_nonexistent)
          return true;

        // Otherwise, if this is not a path_target, then we don't match.
        //
        path_target* pt (mt.is_a<path_target> ());
        if (pt == nullptr)
          return false;

        const path* p (&pt->path ());

        // Assign the path.
        //
        if (p->empty ())
        {
          // Since we cannot come up with an extension, ask the target's
          // derivation function to treat this as prerequisite (just like in
          // search_existing_file()).
          //
          if (pt->derive_extension (true) == nullptr)
          {
            l4 ([&]{trace << "no default extension for target " << *pt;});
            return false;
          }

          p = &pt->derive_path ();
        }

        ts = file_mtime (*p);
        pt->mtime (ts);

        if (ts != timestamp_unknown && ts != timestamp_nonexistent)
          return true;

        l4 ([&]{trace << "no existing file for target " << *pt;});
        return false;
      }
    }
  }

  recipe file_rule::
  apply (action a, target& t) const
  {
    /*
      @@ outer
      return noop_recipe;
    */

    // Update triggers the update of this target's prerequisites so it would
    // seem natural that we should also trigger their cleanup. However, this
    // possibility is rather theoretical so until we see a real use-case for
    // this functionality, we simply ignore the clean operation.
    //
    if (a.operation () == clean_id)
      return noop_recipe;

    // If we have no prerequisites, then this means this file is up to date.
    // Return noop_recipe which will also cause the target's state to be set
    // to unchanged. This is an important optimization on which quite a few
    // places that deal with predominantly static content rely.
    //
    if (!t.has_group_prerequisites ()) // Group as in match_prerequisites().
      return noop_recipe;

    // Match all the prerequisites.
    //
    match_prerequisites (a, t);

    // Note that we used to provide perform_update() which checked that this
    // target is not older than any of its prerequisites. However, later we
    // realized this is probably wrong: consider a script with a testscript as
    // a prerequisite; chances are the testscript will be newer than the
    // script and there is nothing wrong with that.
    //
    return default_recipe;
  }

  const file_rule file_rule::instance;

  // alias_rule
  //
  bool alias_rule::
  match (action, target&, const string&) const
  {
    return true;
  }

  recipe alias_rule::
  apply (action a, target& t) const
  {
    // Inject dependency on our directory (note: not parent) so that it is
    // automatically created on update and removed on clean.
    //
    inject_fsdir (a, t, false);

    match_prerequisites (a, t);
    return default_recipe;
  }

  const alias_rule alias_rule::instance;

  // fsdir_rule
  //
  bool fsdir_rule::
  match (action, target&, const string&) const
  {
    return true;
  }

  recipe fsdir_rule::
  apply (action a, target& t) const
  {
    // Inject dependency on the parent directory. Note that it must be first
    // (see perform_update_direct()).
    //
    inject_fsdir (a, t);

    match_prerequisites (a, t);

    switch (a)
    {
    case perform_update_id: return &perform_update;
    case perform_clean_id: return &perform_clean;
    default: assert (false); return default_recipe;
    }
  }

  static bool
  fsdir_mkdir (const target& t, const dir_path& d)
  {
    // Even with the exists() check below this can still be racy so only print
    // things if we actually did create it (similar to build2::mkdir()).
    //
    auto print = [&t, &d] ()
    {
      if (verb >= 2)
        text << "mkdir " << d;
      else if (verb && current_diag_noise)
        text << "mkdir " << t;
    };

    mkdir_status ms;

    try
    {
      ms = try_mkdir (d);
    }
    catch (const system_error& e)
    {
      print ();
      fail << "unable to create directory " << d << ": " << e << endf;
    }

    if (ms == mkdir_status::success)
    {
      print ();
      return true;
    }

    return false;
  }

  target_state fsdir_rule::
  perform_update (action a, const target& t)
  {
    target_state ts (target_state::unchanged);

    // First update prerequisites (e.g. create parent directories) then create
    // this directory.
    //
    // @@ outer: should we assume for simplicity its only prereqs are fsdir{}?
    //
    if (!t.prerequisite_targets[a].empty ())
      ts = straight_execute_prerequisites (a, t);

    // The same code as in perform_update_direct() below.
    //
    const dir_path& d (t.dir); // Everything is in t.dir.

    // Generally, it is probably correct to assume that in the majority of
    // cases the directory will already exist. If so, then we are going to get
    // better performance by first checking if it indeed exists. See
    // butl::try_mkdir() for details.
    //
    // @@ Also skip prerequisites? Can't we return noop in apply?
    //
    if (!exists (d) && fsdir_mkdir (t, d))
      ts |= target_state::changed;

    return ts;
  }

  void fsdir_rule::
  perform_update_direct (action a, const target& t)
  {
    // First create the parent directory. If present, it is always first.
    //
    const target* p (t.prerequisite_targets[a].empty ()
                     ? nullptr
                     : t.prerequisite_targets[a][0]);

    if (p != nullptr && p->is_a<fsdir> ())
      perform_update_direct (a, *p);

    // The same code as in perform_update() above.
    //
    const dir_path& d (t.dir);

    if (!exists (d))
      fsdir_mkdir (t, d);
  }

  target_state fsdir_rule::
  perform_clean (action a, const target& t)
  {
    // The reverse order of update: first delete this directory, then clean
    // prerequisites (e.g., delete parent directories).
    //
    // Don't fail if we couldn't remove the directory because it is not empty
    // (or is current working directory). In this case rmdir() will issue a
    // warning when appropriate.
    //
    target_state ts (rmdir (t.dir, t, current_diag_noise ? 1 : 2)
                     ? target_state::changed
                     : target_state::unchanged);

    if (!t.prerequisite_targets[a].empty ())
      ts |= reverse_execute_prerequisites (a, t);

    return ts;
  }

  const fsdir_rule fsdir_rule::instance;

  // noop_rule
  //
  bool noop_rule::
  match (action, target&, const string&) const
  {
    return true;
  }

  recipe noop_rule::
  apply (action, target&) const
  {
    return noop_recipe;
  }

  const noop_rule noop_rule::instance;
}
