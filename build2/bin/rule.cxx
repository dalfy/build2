// file      : build2/bin/rule.cxx -*- C++ -*-
// copyright : Copyright (c) 2014-2017 Code Synthesis Ltd
// license   : MIT; see accompanying LICENSE file

#include <build2/bin/rule>

#include <build2/scope>
#include <build2/target>
#include <build2/algorithm>
#include <build2/diagnostics>

#include <build2/bin/target>

using namespace std;

namespace build2
{
  namespace bin
  {
    // obj
    //
    match_result obj_rule::
    match (slock&, action a, target& t, const string&) const
    {
      fail << diag_doing (a, t) << " target group" <<
        info << "explicitly select obje{}, obja{}, or objs{} member";

      return false;
    }

    recipe obj_rule::
    apply (slock&, action, target&) const {return empty_recipe;}

    // lib
    //
    // The whole logic is pretty much as if we had our two group members as
    // our prerequisites.
    //

    struct match_data
    {
      const string& type;
    };

    static_assert (sizeof (match_data) <= target::data_size,
                   "insufficient space");

    match_result lib_rule::
    match (slock& ml, action act, target& xt, const string&) const
    {
      lib& t (static_cast<lib&> (xt));

      // @@ We have to re-query it on each match_only()!

      // Get the library type to build. If not set for a target, this
      // should be configured at the project scope by init().
      //
      const string& type (cast<string> (t["bin.lib"]));

      bool a (type == "static" || type == "both");
      bool s (type == "shared" || type == "both");

      if (!a && !s)
        fail << "unknown library type: " << type <<
          info << "'static', 'shared', or 'both' expected";

      // Search and pre-match the members. The pre-match here is part
      // of the "library meta-information protocol" that could be used
      // by the module that actually builds the members. The idea is
      // that pre-matching members may populate our prerequisite_targets
      // with prerequisite libraries from which others can extract the
      // meta-information about the library, such as the options to use
      // when linking it, etc.
      //
      if (a)
      {
        if (t.a == nullptr)
          t.a = &search<liba> (t.dir, t.out, t.name, nullptr, nullptr);

        match_only (ml, act, *t.a);
      }

      if (s)
      {
        if (t.s == nullptr)
          t.s = &search<libs> (t.dir, t.out, t.name, nullptr, nullptr);

        match_only (ml, act, *t.s);
      }

      t.data (match_data {type}); // Save in the target's auxilary storage.

      match_result mr (true);

      // If there is an outer operation, indicate that we match
      // unconditionally so that we don't override ourselves.
      //
      if (act.outer_operation () != 0)
        mr.recipe_action = action (act.meta_operation (), act.operation ());

      return mr;
    }

    recipe lib_rule::
    apply (slock& ml, action act, target& xt) const
    {
      lib& t (static_cast<lib&> (xt));

      const match_data& md (t.data<match_data> ());
      const string& type (md.type);

      bool a (type == "static" || type == "both");
      bool s (type == "shared" || type == "both");

      // Now we do full match.
      //
      if (a)
        build2::match (ml, act, *t.a);

      if (s)
        build2::match (ml, act, *t.s);

      return &perform;
    }

    target_state lib_rule::
    perform (action act, const target& xt)
    {
      const lib& t (static_cast<const lib&> (xt));

      const match_data& md (t.data<match_data> ());
      const string& type (md.type);

      bool a (type == "static" || type == "both");
      bool s (type == "shared" || type == "both");

      target* m1 (a ? t.a : nullptr);
      target* m2 (s ? t.s : nullptr);

      if (current_mode == execution_mode::last)
        swap (m1, m2);

      target_state r (target_state::unchanged);

      if (m1 != nullptr)
        r |= execute (act, *m1);

      if (m2 != nullptr)
        r |= execute (act, *m2);

      return r;
    }
  }
}
